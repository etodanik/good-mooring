#pragma once
#include "Common/Utilities/Interfaces/IMath.h"
#include <cmath>
namespace mooring
{
// These controls affect rendering only. Resource sizes change on Apply.
struct WaterLook
{
    float overcast = .12f, rain = 0, foam = 1, scattering = 1;
    float shafts = .6f, exposure = 1;
    float bloom = .08f, bloomThreshold = 1;
    float sunAzimuth = 129.2894f, sunElevation = 44.59f;
    float normalFilter = 1, microDetail = 1.3f, roughness = .05f, foamScale = 1;
    float sunStrength = .35f, reflection = 1, refraction = 1, contactFoam = .65f;
    float foamDecay = .22f, foamSpread = .025f, breakingThreshold = .55f, sprayRate = 1;
    float foamRelief = .025f, debugGain = 1;
    float foamBreakup = 1, foamWaveFlow = 1, foamClumpHeight = .18f;
    float volumeContrast = 4, bubbleScattering = 1;
    float seabedColor[3] = { .29f, .31f, .2f };
    int   fftSize = 256, gridSize = 256, shadowSize = 2048, reflectionDivisor = 2, waterLightSize = 512, scatteringSamples = 4;
    int   particles = 2048, shaftSteps = 8, cloudSteps = 32, refractionSteps = 8, reflectionSteps = 64, debugView = 0;
    bool  antialias = true;
    void  sunlight(float4& out) const
    {
        float azimuth = sunAzimuth * .01745329252f, elevation = sunElevation * .01745329252f;
        out[0] = std::cos(azimuth) * std::cos(elevation);
        out[1] = std::sin(elevation);
        out[2] = std::sin(azimuth) * std::cos(elevation);
        out[3] = float(cloudSteps);
    }
    void preset(unsigned level)
    {
        fftSize = level == 2 ? 512 : 256;
        gridSize = level == 0 ? 128 : level == 1 ? 256 : 512;
        shadowSize = level == 0 ? 1024 : level == 1 ? 2048 : 4096;
        reflectionDivisor = level == 0 ? 4 : level == 1 ? 2 : 1;
        particles = level == 0 ? 512 : level == 1 ? 2048 : 4096;
        shaftSteps = level == 0 ? 0 : level == 1 ? 8 : 16;
        cloudSteps = level == 0 ? 16 : level == 1 ? 32 : 64;
        refractionSteps = level == 0 ? 4 : level == 1 ? 8 : 12;
        reflectionSteps = level == 0 ? 32 : level == 1 ? 64 : 128;
        waterLightSize = level == 0 ? 256 : level == 1 ? 512 : 1024;
        scatteringSamples = level == 0 ? 2 : level == 1 ? 4 : 6;
        microDetail = level == 0 ? .65f : 1.3f;
        normalFilter = 1;
        antialias = true;
        foamRelief = level == 0 ? 0 : .025f;
        bloom = level == 0 ? 0 : .08f;
    }
    void parameters(float4& weather, float4& effects) const
    {
        weather[0] = overcast;
        weather[1] = rain;
        weather[2] = foam;
        weather[3] = scattering;
        effects[0] = shafts;
        effects[1] = exposure;
        effects[2] = shaftSteps > 0 ? 1.0f : 0.0f;
        effects[3] = float(debugView);
    }
    void fidelity(float4& detail, float4& quality, float4& optics, float4& foamSettings) const
    {
        detail[0] = normalFilter;
        detail[1] = microDetail;
        detail[2] = roughness;
        detail[3] = foamScale;
        quality[0] = float(particles);
        quality[1] = float(shaftSteps);
        quality[2] = float(shadowSize);
        quality[3] = antialias ? 1.0f : 0.0f;
        optics[0] = sunStrength;
        optics[1] = reflection;
        optics[2] = refraction;
        optics[3] = contactFoam;
        foamSettings[0] = foamDecay;
        foamSettings[1] = foamSpread;
        foamSettings[2] = breakingThreshold;
        foamSettings[3] = sprayRate;
    }
};
} // namespace mooring
