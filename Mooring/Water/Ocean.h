#pragma once
#include "../Simulation/Simulation.h"

namespace mooring
{
constexpr unsigned OceanBands = 3;
constexpr unsigned OceanSpectrumSize = 128;
constexpr unsigned OceanModeCount = OceanBands * OceanSpectrumSize * OceanSpectrumSize;
constexpr float    OceanLengths[OceanBands] = { 256, 64, 16 };
struct SeaState
{
    float    windSpeed = 7, windDirection = .8f, fetch = 3000, gust = 0;
    float    windWaveHeight = .45f, swellHeight = .2f, swellPeriod = 5.5f, swellDirection = -.4f;
    float    depth = 8, level = 0, choppiness = .65f;
    Vec3     current = { .12f, 0, .04f };
    uint32_t seed = 20260913;
};
// GPU layout: one float4 per mode; wave numbers follow its array index.
struct SpectrumMode
{
    float real, imaginary, omega, waveNumber;
};
// One additional 4 m normal-only band; sub-centimetre heights do not drive vessels.
void buildRippleSpectrum(const SeaState& sea, SpectrumMode* modes);
struct Complex
{
    float real, imaginary;
};
constexpr unsigned WavePacketBuckets = 8, WavePacketsPerBucket = 32, MaxWavePackets = WavePacketBuckets * WavePacketsPerBucket;
// Four float4s, shared without repacking with the FSL local-field kernel.
struct WavePacket
{
    float freeX, freeZ, freeAmplitude, phase;
    float x, z, amplitude, age;
    float freeDirectionX, freeDirectionZ, directionX, directionZ;
    float waveNumber, omega, radius, lifetime;
};
// Attached pressure disturbance; four float4s also read by the local-field kernel.
// Wave packets carry the outgoing part after it leaves the hull.
struct HullWake
{
    Vec3     position;
    float    amplitude;
    Vec3     forward;
    float    length;
    Vec3     velocity;
    float    width;
    float    spacing;
    uint32_t sourceBody;
    float    padding[2];
};
constexpr unsigned MaxHullWakes = 16, LocalWaterSize = 256;
struct PacketSample
{
    float height, dx, dz, verticalVelocity, velocityX, velocityZ, accelerationX, accelerationZ;
};
struct WaterSample
{
    float height;
    Vec3  displacement, normal, velocity, acceleration;
};
struct OceanMetrics
{
    float    significantHeight, rmsSlope;
    float    whitecapFraction, breakingThreshold[2][3];
    uint64_t updates;
};
struct Ocean;
Ocean*          createOcean(const SeaState& settings);
void            destroyOcean(Ocean* ocean);
void            configureOcean(Ocean* ocean, const SeaState& settings);
void            updateOcean(Ocean* ocean, double time);
WaterSample     sampleOcean(const Ocean* ocean, float worldX, float worldZ, float depthBelowSurface = 0, uint32_t excludeBody = UINT32_MAX);
WaterSample     sampleOceanBand(const Ocean* ocean, unsigned band, float worldX, float worldZ);
const SeaState& seaState(const Ocean* ocean);
const SpectrumMode* oceanSpectrum(const Ocean* ocean);
const OceanMetrics& oceanMetrics(const Ocean* ocean);
uint32_t            oceanRevision(const Ocean* ocean);
bool         emitWavePacket(Ocean* ocean, Vec3 position, Vec3 direction, float wavelength, float energyJoules, bool backgroundPair = false,
                            uint32_t sourceBody = UINT32_MAX);
void         advanceWavePackets(Ocean* ocean, float dt, Vec3 patchCenter, bool dockReflection = true);
void         clearWavePackets(Ocean* ocean);
PacketSample sampleWavePackets(const Ocean* ocean, float worldX, float worldZ, float depthBelowSurface = 0,
                               uint32_t excludeBody = UINT32_MAX);
const WavePacket* oceanPackets(const Ocean* ocean);
void              setHullWake(Ocean* ocean, unsigned index, const HullWake& wake);
const HullWake*   oceanHullWakes(const Ocean* ocean);
Vec3              oceanPatchCenter(const Ocean* ocean);
unsigned          activeWavePackets(const Ocean* ocean);
unsigned          droppedWavePackets(const Ocean* ocean);
// Unnormalised positive-exponent inverse transform. Scratch and twiddles are caller-owned.
void              inverseFFT(Complex* data, Complex* scratch, const Complex* twiddles, unsigned size);
float             dispersion(float waveNumber, float depth);
float             groupVelocity(float waveNumber, float depth);
float             wakeWavelength(float phaseSpeed, float depth);
Vec3              orbitalAttenuation(float waveNumber, float waterDepth, float depthBelowSurface);
} // namespace mooring
