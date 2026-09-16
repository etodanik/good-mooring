#include "Ocean.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "Common/Utilities/Interfaces/ILog.h"
#include "Common/Utilities/Interfaces/IMemory.h"

namespace mooring
{
constexpr float    Pi = 3.14159265358979323846f;
constexpr unsigned Cells = OceanSpectrumSize * OceanSpectrumSize;
static uint32_t    generation;
struct Surface
{
    float height, dx, dz, vx, vy, vz, ax, ay, az;
};
struct Ocean
{
    SeaState     settings;
    SpectrumMode modes[OceanModeCount];
    Surface      surface[OceanModeCount];
    float        orbitalFactor[OceanModeCount];
    Complex      height[Cells], derivative[Cells], work[Cells], scratch[Cells], twiddles[OceanSpectrumSize / 2];
    float        representativeK[OceanBands];
    OceanMetrics metrics;
    WavePacket   packets[MaxWavePackets];
    HullWake     hullWakes[MaxHullWakes];
    uint32_t     packetOwners[MaxWavePackets]; // Physics only; GPU packets need no ownership data.
    Vec3         patchCenter;
    unsigned     packetCursor[WavePacketBuckets], activePackets, droppedPackets, activeHullWakes;
    uint32_t     revision;
    double       time;
};
static Complex add(Complex left, Complex right) { return { left.real + right.real, left.imaginary + right.imaginary }; }
static Complex sub(Complex left, Complex right) { return { left.real - right.real, left.imaginary - right.imaginary }; }
static Complex mul(Complex left, Complex right)
{
    return { left.real * right.real - left.imaginary * right.imaginary, left.real * right.imaginary + left.imaginary * right.real };
}
static Complex  scale(Complex value, float factor) { return { value.real * factor, value.imaginary * factor }; }
static Complex  timesI(Complex value) { return { -value.imaginary, value.real }; }
static unsigned mirror(unsigned column, unsigned row)
{
    return ((OceanSpectrumSize - row) % OceanSpectrumSize) * OceanSpectrumSize + (OceanSpectrumSize - column) % OceanSpectrumSize;
}
static int signedIndex(unsigned sampleIndex)
{
    return sampleIndex < OceanSpectrumSize / 2 ? int(sampleIndex) : int(sampleIndex) - int(OceanSpectrumSize);
}
float dispersion(float waveNumber, float depth) { return std::sqrt(Gravity * waveNumber * std::tanh(waveNumber * depth)); }
float groupVelocity(float waveNumber, float depth)
{
    if (waveNumber < 1e-6f)
        return std::sqrt(Gravity * depth);
    const float depthFactor = std::tanh(waveNumber * depth), omega = dispersion(waveNumber, depth);
    return Gravity * (depthFactor + waveNumber * depth * (1 - depthFactor * depthFactor)) / (2 * omega);
}
float wakeWavelength(float phaseSpeed, float depth)
{
    if (phaseSpeed <= 0 || phaseSpeed * phaseSpeed >= Gravity * depth)
        return 0;
    // omega/k = U cos(theta), with finite-depth dispersion. At the critical
    // depth speed this direction has no stationary gravity-wave solution.
    float low = 0, high = Gravity / (phaseSpeed * phaseSpeed);
    for (unsigned iteration = 0; iteration < 24; ++iteration)
    {
        float waveNumber = (low + high) * .5f;
        if (Gravity * std::tanh(waveNumber * depth) > waveNumber * phaseSpeed * phaseSpeed)
            low = waveNumber;
        else
            high = waveNumber;
    }
    return 2 * Pi / ((low + high) * .5f);
}
Vec3 orbitalAttenuation(float waveNumber, float waterDepth, float below)
{
    float clampedDepth = std::max(.001f, waterDepth), depthBelowSurface = std::clamp(below, 0.0f, clampedDepth);
    if (waveNumber < 1e-6f)
        return { 1, 1 - depthBelowSurface / clampedDepth, 1 };
    // cosh[k(h-z)]/cosh(kh), sinh[k(h-z)]/sinh(kh), expressed
    // without overflowing exponentials. No vertical flow through the bottom.
    float horizontal = std::exp(-waveNumber * depthBelowSurface) * (1 + std::exp(-2 * waveNumber * (clampedDepth - depthBelowSurface))) /
                       (1 + std::exp(-2 * waveNumber * clampedDepth));
    float vertical = std::exp(-waveNumber * depthBelowSurface) * std::expm1(-2 * waveNumber * (clampedDepth - depthBelowSurface)) /
                     std::expm1(-2 * waveNumber * clampedDepth);
    return { horizontal, vertical, horizontal };
}
void inverseFFT(Complex* data, Complex* scratch, const Complex* twiddles, unsigned size)
{
    MTRACY_ZONE("inverseFFT");
    // Stockham autosort: each pass reads two contiguous half-arrays and
    // interleaves butterflies. No bit reversal, temporary heap, or trig in the passes.
    Complex* src = data;
    Complex* dst = scratch;
    for (unsigned axis = 0; axis < 2; ++axis)
    {
        for (unsigned stride = 1; stride < size; stride *= 2)
        {
            // Traverse contiguous columns together on the vertical axis.
            // Butterflies within a stage are independent; their arithmetic
            // stays identical while both reads and writes become sequential.
            for (unsigned major = 0; major < (axis ? size / 2 : size); ++major)
                for (unsigned minor = 0; minor < (axis ? size : size / 2); ++minor)
                {
                    unsigned row = axis ? minor : major, butterflyIndex = axis ? major : minor;
                    unsigned butterflyOffset = butterflyIndex & (stride - 1),
                             output = 2 * (butterflyIndex - butterflyOffset) + butterflyOffset;
                    unsigned firstInput = axis ? butterflyIndex * size + row : row * size + butterflyIndex;
                    unsigned secondInput = axis ? (butterflyIndex + size / 2) * size + row : firstInput + size / 2;
                    unsigned lo = axis ? output * size + row : row * size + output;
                    unsigned hi = axis ? lo + stride * size : lo + stride;
                    Complex  rotated = mul(src[secondInput], twiddles[butterflyOffset * (size / (2 * stride))]);
                    dst[lo] = add(src[firstInput], rotated);
                    dst[hi] = sub(src[firstInput], rotated);
                }
            std::swap(src, dst);
        }
    }
    if (src != data)
        memcpy(data, src, size * size * sizeof(Complex));
}
static uint32_t hash(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}
static float   uniform(uint32_t key) { return (float(hash(key) >> 8) + .5f) / 16777216.0f; }
static Complex gaussian(uint32_t key)
{
    float radius = std::sqrt(-2 * std::log(uniform(key))), phase = 2 * Pi * uniform(key ^ 0x9e3779b9u);
    return { radius * std::cos(phase), radius * std::sin(phase) };
}
void buildRippleSpectrum(const SeaState& sea, SpectrumMode* modes)
{
    MTRACY_ZONE("buildRippleSpectrum");
    double slopeVariance = 0;
    for (unsigned row = 0; row < OceanSpectrumSize; ++row)
        for (unsigned column = 0; column < OceanSpectrumSize; ++column)
        {
            unsigned sampleIndex = row * OceanSpectrumSize + column;
            modes[sampleIndex] = {};
            float kx = signedIndex(column) * Pi, kz = signedIndex(row) * Pi, waveNumber = std::hypot(kx, kz);
            if (waveNumber < 2 * Pi / .30f || waveNumber > 2 * Pi / .035f || column == OceanSpectrumSize / 2 ||
                row == OceanSpectrumSize / 2)
                continue;
            float alignment = (kx * std::cos(sea.windDirection) + kz * std::sin(sea.windDirection)) / waveNumber;
            float power = std::exp(-waveNumber * waveNumber * .000025f) * (.15f + .85f * alignment * alignment) /
                          (waveNumber * waveNumber * waveNumber * waveNumber);
            auto amplitude = scale(gaussian(sampleIndex ^ sea.seed ^ 0xb82415afu), std::sqrt(power));
            modes[sampleIndex] = { amplitude.real, amplitude.imaginary,
                                   std::sqrt(Gravity * waveNumber + .000074f * waveNumber * waveNumber * waveNumber), waveNumber };
            slopeVariance += 2 * (amplitude.real * amplitude.real + amplitude.imaginary * amplitude.imaginary) * waveNumber * waveNumber;
        }
    // Short wind ripples disappear before long swell does. A square-root wind
    // ramp left conspicuous capillary texture even in the calm preset.
    float wind = std::clamp((sea.windSpeed - .75f) / 4.25f, 0.0f, 1.0f);
    float rmsSlope = .065f * wind * wind * (3 - 2 * wind);
    float scaleFactor = slopeVariance > 0 ? rmsSlope / std::sqrt(slopeVariance) : 0;
    for (unsigned sampleIndex = 0; sampleIndex < Cells; ++sampleIndex)
    {
        modes[sampleIndex].real *= scaleFactor;
        modes[sampleIndex].imaginary *= scaleFactor;
    }
}
static float spreading(float theta, float direction, float exponent)
{
    float angle = std::remainder(theta - direction, 2 * Pi);
    float normalization = std::exp(std::lgamma(exponent + 1) - std::lgamma(exponent + .5f)) / (2 * std::sqrt(Pi));
    return normalization * std::pow(std::max(0.0f, std::cos(angle * .5f)), 2 * exponent);
}
static float density(const SeaState& sea, float omega, float theta, bool swell)
{
    float peak = swell ? 2 * Pi / sea.swellPeriod : 22 * std::cbrt(Gravity * Gravity / (std::max(.5f, sea.windSpeed) * sea.fetch));
    float ratio = omega / peak;
    if (ratio < .25f)
        return 0;
    float sigma = omega <= peak ? .07f : .09f;
    float peakRatio = std::exp(-.5f * std::pow((omega - peak) / (sigma * peak), 2));
    float spectrum = std::pow(omega, -5) * std::exp(-1.25f * std::pow(peak / omega, 4)) * std::pow(swell ? 5.0f : 3.3f, peakRatio);
    float spread = swell ? 28 : std::clamp(16 * std::pow(ratio, omega <= peak ? 5.0f : -2.5f), 1.0f, 32.0f);
    return spectrum * spreading(theta, swell ? sea.swellDirection : sea.windDirection, spread);
}
static Surface interpolate(const Ocean* ocean, unsigned band, float worldX, float worldZ);
static void    calibrateWhitecaps(Ocean* ocean)
{
    MTRACY_ZONE("calibrateWhitecaps");
    // Tessendorf/Reinhardt/Gao: choose a minimum-stretch threshold from the
    // realised distribution and an observed wind/whitecap relation. This is
    // configuration work; no histogram, allocation or readback runs per frame.
    float wind = ocean->settings.windSpeed;
    float fraction = wind < 3 ? 0 : std::min(.35f, 3.84e-6f * std::pow(wind, 3.41f));
    ocean->metrics.whitecapFraction = fraction;
    for (auto& thresholds : ocean->metrics.breakingThreshold)
    {
        thresholds[0] = thresholds[2] = 0;
        thresholds[1] = -2;
    }
    if (fraction == 0)
        return;
    for (unsigned preset = 0; preset < 2; ++preset)
    {
        // Match each GPU's central derivative in frequency space. Differencing
        // the coarser CPU grid underestimated short-wave compression severely.
        for (unsigned band = 0; band < OceanBands; ++band)
            for (unsigned field = 0; field < 2; ++field)
            {
                float       delta = 2 * Pi / OceanLengths[band], spacing = OceanLengths[band] / float(256u << preset);
                const auto* modes = ocean->modes + band * Cells;
                for (unsigned row = 0; row < OceanSpectrumSize; ++row)
                    for (unsigned column = 0; column < OceanSpectrumSize; ++column)
                    {
                        unsigned sampleIndex = row * OceanSpectrumSize + column;
                        auto     mode = modes[sampleIndex], mirroredMode = modes[mirror(column, row)];
                        Complex  heightSpectrum = { mode.real + mirroredMode.real, mode.imaginary - mirroredMode.imaginary };
                        float    kx = signedIndex(column) * delta, kz = signedIndex(row) * delta;
                        float    dx = std::sin(kx * spacing) / spacing, dz = std::sin(kz * spacing) / spacing;
                        float    factor = -ocean->orbitalFactor[band * Cells + sampleIndex] / delta * ocean->settings.choppiness;
                        ocean->work[sampleIndex] =
                            field == 0 ? add(scale(heightSpectrum, kx * dx * factor), timesI(scale(heightSpectrum, kz * dz * factor)))
                                       : scale(heightSpectrum, (kx * dz + kz * dx) * .5f * factor);
                    }
                inverseFFT(ocean->work, ocean->scratch, ocean->twiddles, OceanSpectrumSize);
                for (unsigned sampleIndex = 0; sampleIndex < Cells; ++sampleIndex)
                {
                    auto& sample = ocean->surface[band * Cells + sampleIndex];
                    if (field == 0)
                    {
                        sample.ax = ocean->work[sampleIndex].real;
                        sample.ay = ocean->work[sampleIndex].imaginary;
                    }
                    else
                        sample.az = ocean->work[sampleIndex].real;
                }
            }
        auto& thresholds = ocean->metrics.breakingThreshold[preset];
        for (unsigned band : { 0u, 2u })
        {
            unsigned histogram[1024] = {}, size = band == 0 ? 256u << preset : OceanSpectrumSize;
            float    spacing = OceanLengths[band] / size;
            for (unsigned row = 0; row < size; ++row)
                for (unsigned column = 0; column < size; ++column)
                {
                    auto gradient = interpolate(ocean, band, column * spacing, row * spacing);
                    if (band == 0)
                    {
                        auto medium = interpolate(ocean, 1, column * spacing, row * spacing);
                        gradient.ax += medium.ax;
                        gradient.ay += medium.ay;
                        gradient.az += medium.az;
                    }
                    float    stretch = 1 + (gradient.ax + gradient.ay - std::hypot(gradient.ax - gradient.ay, 2 * gradient.az)) * .5f;
                    unsigned bin = unsigned(std::clamp((stretch + 2) * 256, 0.0f, 1023.0f));
                    ++histogram[bin];
                }
            // Split coverage between larger crests and short whitecaps. This is
            // visual tuning; the wind relation is not an exact coverage law.
            // Account for the shorter remnant lifetime: the former .861
            // feedback factor under-filled active caps after decay increased.
            float    coverage = fraction * (band == 0 ? .85f : .15f), alpha = .70f;
            float    quantile = coverage * (1 - alpha) / (1 - alpha * coverage) * size * size;
            unsigned bin = 0, total = histogram[0];
            while (total < quantile && bin < 1023)
                total += histogram[++bin];
            thresholds[band] = std::min(.98f, (bin + .5f) / 256 - 2);
        }
    }
}
void configureOcean(Ocean* ocean, const SeaState& settings)
{
    MTRACY_ZONE("configureOcean");
    ocean->settings = settings;
    auto& sea = ocean->settings;
    sea.depth = std::max(.5f, sea.depth);
    sea.fetch = std::max(50.0f, sea.fetch);
    sea.swellPeriod = std::clamp(sea.swellPeriod, 1.5f, 18.0f);
    sea.windWaveHeight = std::max(0.0f, sea.windWaveHeight);
    sea.swellHeight = std::max(0.0f, sea.swellHeight);
    sea.choppiness = std::clamp(sea.choppiness, 0.0f, 1.0f);
    memset(ocean->modes, 0, sizeof ocean->modes);
    memset(ocean->surface, 0, sizeof ocean->surface);
    ocean->metrics = {};
    clearWavePackets(ocean);
    ocean->patchCenter = {};
    for (unsigned twiddleIndex = 0; twiddleIndex < OceanSpectrumSize / 2; ++twiddleIndex)
    {
        float angle = 2 * Pi * twiddleIndex / OceanSpectrumSize;
        ocean->twiddles[twiddleIndex] = { std::cos(angle), std::sin(angle) };
    }
    // Separate, non-overlapping wavelength bands preserve total spectral energy.
    // The 0.30 m cutoff is shared by all rendering presets and CPU queries.
    constexpr float limits[] = { 0, 2 * Pi / 16, 2 * Pi / 4, 2 * Pi / .30f };
    for (unsigned source = 0; source < 2; ++source)
    {
        double variance = 0;
        for (unsigned band = 0; band < OceanBands; ++band)
        {
            float delta = 2 * Pi / OceanLengths[band];
            for (unsigned row = 0; row < OceanSpectrumSize; ++row)
                for (unsigned column = 0; column < OceanSpectrumSize; ++column)
                {
                    unsigned idx = band * Cells + row * OceanSpectrumSize + column;
                    float    kx = signedIndex(column) * delta, kz = signedIndex(row) * delta, waveNumber = std::hypot(kx, kz);
                    Complex  amplitude = {};
                    if (waveNumber > limits[band] && waveNumber <= limits[band + 1] && column != OceanSpectrumSize / 2 &&
                        row != OceanSpectrumSize / 2)
                    {
                        float omega = dispersion(waveNumber, sea.depth);
                        float power = density(sea, omega, std::atan2(kz, kx), source == 1) * groupVelocity(waveNumber, sea.depth) /
                                      waveNumber * delta * delta;
                        // A smooth short-wave rolloff avoids concentrating slope
                        // energy at the hard 30 cm cutoff (Tessendorf, Eq. 41).
                        // The separate fine band supplies wind-driven ripples.
                        power *= std::exp(-waveNumber * waveNumber * .12f * .12f);
                        amplitude = scale(gaussian(idx ^ sea.seed ^ (source * 0x1234567u)), std::sqrt(std::max(0.0f, power) * .25f));
                        ocean->modes[idx].omega = omega;
                        ocean->modes[idx].waveNumber = waveNumber;
                    }
                    // Reuse the surface storage during construction; no temporary allocation.
                    ocean->surface[idx].dx = amplitude.real;
                    ocean->surface[idx].dz = amplitude.imaginary;
                    variance += 2 * (amplitude.real * amplitude.real + amplitude.imaginary * amplitude.imaginary);
                }
        }
        float height = source ? sea.swellHeight : sea.windWaveHeight;
        float normalization = variance > 0 ? height / (4 * std::sqrt(variance)) : 0;
        for (unsigned sampleIndex = 0; sampleIndex < OceanModeCount; ++sampleIndex)
        {
            ocean->modes[sampleIndex].real += ocean->surface[sampleIndex].dx * normalization;
            ocean->modes[sampleIndex].imaginary += ocean->surface[sampleIndex].dz * normalization;
        }
    }
    double energy = 0, slopeEnergy = 0;
    for (unsigned band = 0; band < OceanBands; ++band)
    {
        double bandEnergy = 0, weightedK = 0;
        for (unsigned sampleIndex = 0; sampleIndex < Cells; ++sampleIndex)
        {
            auto mode = ocean->modes[band * Cells + sampleIndex];
            // Geometry and water depth only change on configuration. Avoid
            // repeating this transcendental in every field of every physics step.
            ocean->orbitalFactor[band * Cells + sampleIndex] =
                mode.waveNumber > 0 ? 2 * Pi / OceanLengths[band] / mode.waveNumber / std::tanh(mode.waveNumber * sea.depth) : 0;
            double power = 2 * (mode.real * mode.real + mode.imaginary * mode.imaginary);
            bandEnergy += power;
            weightedK += power * mode.waveNumber;
            slopeEnergy += power * mode.waveNumber * mode.waveNumber;
        }
        ocean->representativeK[band] = bandEnergy > 0 ? weightedK / bandEnergy : 0;
        energy += bandEnergy;
    }
    ocean->metrics.significantHeight = 4 * std::sqrt(energy);
    ocean->metrics.rmsSlope = std::sqrt(slopeEnergy);
    memset(ocean->surface, 0, sizeof ocean->surface);
    ocean->revision = ++generation;
    calibrateWhitecaps(ocean);
    updateOcean(ocean, 0); // Restore acceleration fields used as calibration scratch.
}
Ocean* createOcean(const SeaState& settings)
{
    MTRACY_ZONE("createOcean");
    auto* ocean = tf_new(Ocean);
    configureOcean(ocean, settings);
    return ocean;
}
void destroyOcean(Ocean* ocean)
{
    MTRACY_ZONE("destroyOcean");
    tf_delete(ocean);
}
void updateOcean(Ocean* ocean, double time)
{
    MTRACY_ZONE("updateOcean");
    ocean->time = time;
    ++ocean->metrics.updates;
    if (ocean->metrics.significantHeight == 0)
        return;
    for (unsigned band = 0; band < OceanBands; ++band)
    {
        auto* modes = ocean->modes + band * Cells;
        auto* surface = ocean->surface + band * Cells;
        for (unsigned row = 0; row < OceanSpectrumSize; ++row)
            for (unsigned column = 0; column < OceanSpectrumSize; ++column)
            {
                unsigned sampleIndex = row * OceanSpectrumSize + column;
                auto     mode = modes[sampleIndex], mirroredMode = modes[mirror(column, row)];
                if (mode.real == 0 && mode.imaginary == 0 && mirroredMode.real == 0 && mirroredMode.imaginary == 0)
                {
                    ocean->height[sampleIndex] = ocean->derivative[sampleIndex] = {};
                    continue;
                }
                float   phase = std::remainder(mode.omega * time, 2.0 * Pi);
                Complex wavePhase = { std::cos(phase), -std::sin(phase) };
                auto    first = mul({ mode.real, mode.imaginary }, wavePhase);
                auto    second = mul({ mirroredMode.real, -mirroredMode.imaginary }, { wavePhase.real, -wavePhase.imaginary });
                float   advection =
                    std::remainder((signedIndex(column) * ocean->settings.current.x + signedIndex(row) * ocean->settings.current.z) *
                                       (2 * Pi / OceanLengths[band]) * time,
                                   2.0 * Pi);
                Complex currentPhase = { std::cos(advection), -std::sin(advection) };
                ocean->height[sampleIndex] = mul(add(first, second), currentPhase);
                // Orbital velocity is the material derivative; uniform-current
                // advection affects phase, not the intrinsic orbital frequency.
                ocean->derivative[sampleIndex] = mul(scale(timesI(sub(second, first)), mode.omega), currentPhase);
            }
        for (unsigned field = 0; field < 5; ++field)
        {
            for (unsigned row = 0; row < OceanSpectrumSize; ++row)
                for (unsigned column = 0; column < OceanSpectrumSize; ++column)
                {
                    unsigned sampleIndex = row * OceanSpectrumSize + column;
                    auto     heightSpectrum = ocean->height[sampleIndex], dh = ocean->derivative[sampleIndex];
                    if (field == 0)
                        ocean->work[sampleIndex] = add(heightSpectrum, timesI(dh));
                    else if (field == 4)
                        ocean->work[sampleIndex] = scale(heightSpectrum, -modes[sampleIndex].omega * modes[sampleIndex].omega);
                    else
                    {
                        float factor = ocean->orbitalFactor[band * Cells + sampleIndex];
                        auto  horizontal = timesI(field == 2 ? dh : heightSpectrum);
                        if (field == 3)
                            horizontal = scale(horizontal, -modes[sampleIndex].omega * modes[sampleIndex].omega);
                        ocean->work[sampleIndex] =
                            add(scale(horizontal, signedIndex(column) * factor), timesI(scale(horizontal, signedIndex(row) * factor)));
                    }
                }
            inverseFFT(ocean->work, ocean->scratch, ocean->twiddles, OceanSpectrumSize);
            for (unsigned sampleIndex = 0; sampleIndex < Cells; ++sampleIndex)
            {
                auto fieldValue = ocean->work[sampleIndex];
                if (field == 0)
                {
                    surface[sampleIndex].height = fieldValue.real;
                    surface[sampleIndex].vy = fieldValue.imaginary;
                }
                else if (field == 1)
                {
                    surface[sampleIndex].dx = fieldValue.real * ocean->settings.choppiness;
                    surface[sampleIndex].dz = fieldValue.imaginary * ocean->settings.choppiness;
                }
                else if (field == 2)
                {
                    surface[sampleIndex].vx = fieldValue.real;
                    surface[sampleIndex].vz = fieldValue.imaginary;
                }
                else if (field == 3)
                {
                    surface[sampleIndex].ax = fieldValue.real;
                    surface[sampleIndex].az = fieldValue.imaginary;
                }
                else
                    surface[sampleIndex].ay = fieldValue.real;
            }
        }
    }
}
static Surface interpolate(const Ocean* ocean, unsigned band, float worldX, float worldZ)
{
    float       gridX = worldX / OceanLengths[band] * OceanSpectrumSize, gridZ = worldZ / OceanLengths[band] * OceanSpectrumSize;
    int         ix = int(std::floor(gridX)), iy = int(std::floor(gridZ));
    float       tx = gridX - std::floor(gridX), ty = gridZ - std::floor(gridZ);
    const auto* field = ocean->surface + band * Cells;
    Surface     out = {};
    for (unsigned rowOffset = 0; rowOffset < 2; ++rowOffset)
        for (unsigned columnOffset = 0; columnOffset < 2; ++columnOffset)
        {
            float       weight = (columnOffset ? tx : 1 - tx) * (rowOffset ? ty : 1 - ty);
            const auto& sample = field[((iy + int(rowOffset)) & (OceanSpectrumSize - 1)) * OceanSpectrumSize +
                                       ((ix + int(columnOffset)) & (OceanSpectrumSize - 1))];
            out.height += sample.height * weight;
            out.dx += sample.dx * weight;
            out.dz += sample.dz * weight;
            out.vx += sample.vx * weight;
            out.vy += sample.vy * weight;
            out.vz += sample.vz * weight;
            out.ax += sample.ax * weight;
            out.ay += sample.ay * weight;
            out.az += sample.az * weight;
        }
    return out;
}
WaterSample sampleOceanBand(const Ocean* ocean, unsigned band, float worldX, float worldZ)
{
    MTRACY_FINE_ZONE("sampleOceanBand");
    auto  center = interpolate(ocean, band, worldX, worldZ);
    float step = OceanLengths[band] / OceanSpectrumSize;
    auto  left = interpolate(ocean, band, worldX - step, worldZ), right = interpolate(ocean, band, worldX + step, worldZ);
    auto  back = interpolate(ocean, band, worldX, worldZ - step), front = interpolate(ocean, band, worldX, worldZ + step);
    float dx = (right.height - left.height) / (2 * step), dz = (front.height - back.height) / (2 * step);
    float jxx = 1 + (right.dx - left.dx) / (2 * step), jzz = 1 + (front.dz - back.dz) / (2 * step);
    float jxz = (front.dx - back.dx) / (2 * step), jzx = (right.dz - left.dz) / (2 * step);
    float determinant = std::max(.25f, jxx * jzz - jxz * jzx);
    return { center.height,
             { center.dx, 0, center.dz },
             { -(dx * jzz - dz * jzx) / determinant, 1, -(dz * jxx - dx * jxz) / determinant },
             { center.vx, center.vy, center.vz },
             { center.ax, center.ay, center.az } };
}
WaterSample sampleOcean(const Ocean* ocean, float worldX, float worldZ, float depth, uint32_t excludeBody)
{
    MTRACY_FINE_ZONE("sampleOcean");
    if (ocean->metrics.significantHeight == 0 && !ocean->activePackets && !ocean->activeHullWakes)
        return { ocean->settings.level, {}, { 0, 1, 0 }, ocean->settings.current, {} };
    WaterSample out = {};
    out.height = ocean->settings.level;
    out.normal = { 0, 1, 0 };
    out.velocity = ocean->settings.current;
    if (ocean->metrics.significantHeight > 0)
    {
        // Invert horizontal displacement to query at the visible surface's
        // world coordinate. Flat-water wakes need none of these FFT lookups.
        float qx = worldX, qz = worldZ;
        for (unsigned iteration = 0; iteration < 3; ++iteration)
        {
            float dx = 0, dz = 0;
            for (unsigned band = 0; band < OceanBands; ++band)
            {
                auto sample = interpolate(ocean, band, qx, qz);
                dx += sample.dx;
                dz += sample.dz;
            }
            qx = worldX - dx;
            qz = worldZ - dz;
        }
        for (unsigned band = 0; band < OceanBands; ++band)
        {
            auto sample = sampleOceanBand(ocean, band, qx, qz);
            out.height += sample.height;
            out.displacement.x += sample.displacement.x;
            out.displacement.z += sample.displacement.z;
            out.normal.x += sample.normal.x;
            out.normal.z += sample.normal.z;
            // Band-filtered orbital velocity uses the energy-weighted wave
            // number; the surface solution itself uses all spectral modes.
            auto attenuation = orbitalAttenuation(ocean->representativeK[band], ocean->settings.depth, depth);
            out.velocity.x += sample.velocity.x * attenuation.x;
            out.velocity.y += sample.velocity.y * attenuation.y;
            out.velocity.z += sample.velocity.z * attenuation.z;
            out.acceleration.x += sample.acceleration.x * attenuation.x;
            out.acceleration.y += sample.acceleration.y * attenuation.y;
            out.acceleration.z += sample.acceleration.z * attenuation.z;
        }
    }
    auto local = sampleWavePackets(ocean, worldX, worldZ, depth, excludeBody);
    out.height += local.height;
    out.normal.x -= local.dx;
    out.normal.z -= local.dz;
    out.velocity.y += local.verticalVelocity;
    out.velocity.x += local.velocityX;
    out.velocity.z += local.velocityZ;
    out.acceleration.x += local.accelerationX;
    out.acceleration.z += local.accelerationZ;
    float length = std::sqrt(out.normal.x * out.normal.x + 1 + out.normal.z * out.normal.z);
    out.normal.x /= length;
    out.normal.y /= length;
    out.normal.z /= length;
    return out;
}
bool emitWavePacket(Ocean* ocean, Vec3 position, Vec3 direction, float wavelength, float energy, bool pair, uint32_t sourceBody)
{
    MTRACY_ZONE("emitWavePacket");
    if (energy <= 0)
        return true;
    float norm = std::hypot(direction.x, direction.z);
    if (norm < .001f)
        return false;
    // Frequencies start in their own bucket, then borrow unused slots. A pair
    // of hulls must not lose one side of its wake while other buckets are empty.
    unsigned bucket = unsigned(std::clamp(std::round(std::log2(16 / std::clamp(wavelength, .5f, 16.0f)) * 1.4f), 0.0f, 7.0f));
    // Buckets control capacity, not frequency. Rounding wavelength broke
    // phase coherence between emissions from a steadily moving hull.
    float    lambda = std::clamp(wavelength, .5f, 32.0f), radius = lambda * .75f;
    unsigned slot = MaxWavePackets;
    for (unsigned packetIndex = 0; packetIndex < MaxWavePackets; ++packetIndex)
    {
        unsigned candidate = (bucket * WavePacketsPerBucket + ocean->packetCursor[bucket] + packetIndex) % MaxWavePackets;
        if (ocean->packets[candidate].lifetime <= 0)
        {
            slot = candidate;
            ocean->packetCursor[bucket] = (ocean->packetCursor[bucket] + packetIndex + 1) % MaxWavePackets;
            break;
        }
    }
    if (slot == MaxWavePackets)
    {
        ++ocean->droppedPackets;
        return false;
    }
    // The compact (1-r^2/R^2)^2 envelope has an integral of its square of pi R^2/5.
    // Averaging cos^2 over the carrier gives E ~= rho g A^2 pi R^2 / 10.
    float      amplitude = std::min(lambda * .06f, std::sqrt(10 * energy / (WaterDensity * Gravity * Pi * radius * radius)));
    WavePacket packet = {};
    packet.freeX = packet.x = position.x;
    packet.freeZ = packet.z = position.z;
    packet.amplitude = amplitude;
    packet.freeAmplitude = pair ? amplitude : 0;
    packet.freeDirectionX = packet.directionX = direction.x / norm;
    packet.freeDirectionZ = packet.directionZ = direction.z / norm;
    packet.waveNumber = 2 * Pi / lambda;
    packet.omega = dispersion(packet.waveNumber, ocean->settings.depth);
    packet.radius = radius;
    packet.lifetime = 12;
    ocean->packets[slot] = packet;
    ocean->packetOwners[slot] = sourceBody;
    ++ocean->activePackets;
    return true;
}
void advanceWavePackets(Ocean* ocean, float dt, Vec3 center, bool reflectDock)
{
    MTRACY_ZONE("advanceWavePackets");
    ocean->patchCenter = center;
    for (unsigned packetIndex = 0; packetIndex < MaxWavePackets; ++packetIndex)
    {
        auto& packet = ocean->packets[packetIndex];
        if (packet.lifetime <= 0)
            continue;
        packet.age += dt;
        if (packet.age >= packet.lifetime)
        {
            packet.lifetime = 0;
            --ocean->activePackets;
            continue;
        }
        float speed = groupVelocity(packet.waveNumber, ocean->settings.depth);
        packet.freeX += (packet.freeDirectionX * speed + ocean->settings.current.x) * dt;
        packet.freeZ += (packet.freeDirectionZ * speed + ocean->settings.current.z) * dt;
        float oldX = packet.x;
        packet.x += (packet.directionX * speed + ocean->settings.current.x) * dt;
        packet.z += (packet.directionZ * speed + ocean->settings.current.z) * dt;
        packet.phase = std::remainder(packet.phase + (packet.waveNumber * speed - packet.omega) * dt, 2 * Pi);
        // The prototype dock has vertical faces at x=8.8 and x=11.2, z +/-15.
        if (reflectDock && std::fabs(packet.z) < 15 && ((oldX < 8.8f && packet.x >= 8.8f) || (oldX > 11.2f && packet.x <= 11.2f)))
        {
            float wall = oldX < 8.8f ? 8.8f : 11.2f;
            packet.x = 2 * wall - packet.x;
            packet.directionX = -packet.directionX;
            packet.amplitude *= .8f;
            ocean->packetOwners[packetIndex] = UINT32_MAX; // A reflected wake is now an incoming wave.
        }
    }
}
void clearWavePackets(Ocean* ocean)
{
    MTRACY_ZONE("clearWavePackets");
    memset(ocean->packets, 0, sizeof ocean->packets);
    memset(ocean->packetCursor, 0, sizeof ocean->packetCursor);
    memset(ocean->hullWakes, 0, sizeof ocean->hullWakes);
    ocean->activePackets = ocean->droppedPackets = ocean->activeHullWakes = 0;
}
static PacketSample packetContribution(const WavePacket& packet, float worldX, float worldZ, bool free, float depth, float below)
{
    MTRACY_FINE_ZONE("packetContribution");
    float amplitude = free ? packet.freeAmplitude : packet.amplitude;
    float dx = worldX - (free ? packet.freeX : packet.x), dz = worldZ - (free ? packet.freeZ : packet.z);
    float r2 = (dx * dx + dz * dz) / (packet.radius * packet.radius);
    if (r2 >= 1 || amplitude == 0)
        return {};
    float nx = free ? packet.freeDirectionX : packet.directionX, nz = free ? packet.freeDirectionZ : packet.directionZ;
    float envelope = (1 - r2) * (1 - r2) * std::exp(-.18f * packet.age);
    // Fade the last second to zero instead of deleting a finite wave crest.
    float remaining = std::clamp(packet.lifetime - packet.age, 0.0f, 1.0f);
    float life = remaining * remaining * (3 - 2 * remaining), lifeRate = -6 * remaining * (1 - remaining);
    float phase = packet.waveNumber * (dx * nx + dz * nz) + packet.phase, cosine = std::cos(phase), sine = std::sin(phase);
    float common = amplitude * life;
    float envelopeDx = -4 * dx / (packet.radius * packet.radius) * (1 - r2) * std::exp(-.18f * packet.age);
    float envelopeDz = -4 * dz / (packet.radius * packet.radius) * (1 - r2) * std::exp(-.18f * packet.age);
    float envelopeRate = -groupVelocity(packet.waveNumber, depth) * (nx * envelopeDx + nz * envelopeDz) - .18f * envelope;
    auto  attenuation = orbitalAttenuation(packet.waveNumber, depth, below);
    float horizontal = common * envelope * packet.omega / std::tanh(packet.waveNumber * depth) * cosine * attenuation.x;
    return { common * envelope * cosine,
             common * (envelopeDx * cosine - envelope * packet.waveNumber * nx * sine),
             common * (envelopeDz * cosine - envelope * packet.waveNumber * nz * sine),
             (common * (envelopeRate * cosine + envelope * packet.omega * sine) + amplitude * lifeRate * envelope * cosine) * attenuation.y,
             horizontal * nx,
             horizontal * nz,
             -Gravity * common * (envelopeDx * cosine - envelope * packet.waveNumber * nx * sine) * attenuation.x,
             -Gravity * common * (envelopeDz * cosine - envelope * packet.waveNumber * nz * sine) * attenuation.x };
}
PacketSample sampleWavePackets(const Ocean* ocean, float worldX, float worldZ, float below, uint32_t excludeBody)
{
    MTRACY_FINE_ZONE("sampleWavePackets");
    PacketSample value = {};
    float        tx = std::clamp((32 - std::fabs(worldX - ocean->patchCenter.x)) / 8, 0.0f, 1.0f);
    float        tz = std::clamp((32 - std::fabs(worldZ - ocean->patchCenter.z)) / 8, 0.0f, 1.0f);
    float        bx = tx * tx * (3 - 2 * tx), bz = tz * tz * (3 - 2 * tz), blend = bx * bz;
    if (blend == 0)
        return value;
    for (const auto& wake : ocean->hullWakes)
    {
        if (wake.amplitude <= 0 || (excludeBody != UINT32_MAX && wake.sourceBody == excludeBody))
            continue;
        const float     rx = worldX - wake.position.x, rz = worldZ - wake.position.z;
        const float     along = rx * wake.forward.x + rz * wake.forward.z;
        const float     across = rx * wake.forward.z - rz * wake.forward.x;
        const unsigned  hulls = wake.spacing > 0 ? 2 : 1;
        const float     vertical = orbitalAttenuation(2 * Pi / wake.length, ocean->settings.depth, below).y;
        // A bounded moving-pressure approximation: bow pile-up, shoulder
        // drawdown and stern recovery. Dynamic head sets the amplitude; it
        // vanishes at rest and reverses with the hull's through-water motion.
        // Equal positive and negative longitudinal integrals avoid a net mound.
        constexpr float terms[][3] = { { .40f, .105f, 1 }, { 0, .28f, -.1545f / .28f }, { -.46f, .09f, .55f } };
        for (unsigned hull = 0; hull < hulls; ++hull)
        {
            float cross = across + (hull == 0 ? -.5f : .5f) * wake.spacing;
            float width = wake.width * .62f + .3f, crossFactor = std::exp(-cross * cross / (width * width));
            if (crossFactor < .00001f)
                continue;
            for (const auto& term : terms)
            {
                float span = wake.length * term[1], offset = along - wake.length * term[0];
                float height = wake.amplitude * term[2] * std::exp(-offset * offset / (span * span)) * crossFactor;
                float da = -2 * offset / (span * span) * height, dc = -2 * cross / (width * width) * height;
                float dx = da * wake.forward.x + dc * wake.forward.z, dz = da * wake.forward.z - dc * wake.forward.x;
                value.height += height;
                value.dx += dx;
                value.dz += dz;
                value.verticalVelocity +=
                    ((ocean->settings.current.x - wake.velocity.x) * dx + (ocean->settings.current.z - wake.velocity.z) * dz) * vertical;
            }
        }
    }
    if (ocean->activePackets)
        for (unsigned packetIndex = 0; packetIndex < MaxWavePackets; ++packetIndex)
        {
            const auto& packet = ocean->packets[packetIndex];
            if (packet.lifetime <= 0 || (excludeBody != UINT32_MAX && ocean->packetOwners[packetIndex] == excludeBody))
                continue;
            auto actual = packetContribution(packet, worldX, worldZ, false, ocean->settings.depth, below),
                 free = packetContribution(packet, worldX, worldZ, true, ocean->settings.depth, below);
            value.height += actual.height - free.height;
            value.dx += actual.dx - free.dx;
            value.dz += actual.dz - free.dz;
            value.verticalVelocity += actual.verticalVelocity - free.verticalVelocity;
            value.velocityX += actual.velocityX - free.velocityX;
            value.velocityZ += actual.velocityZ - free.velocityZ;
            value.accelerationX += actual.accelerationX - free.accelerationX;
            value.accelerationZ += actual.accelerationZ - free.accelerationZ;
        }
    // Separate smooth edge weights also keep the derivative continuous at
    // patch corners. A max(abs(x),abs(z)) fade left diagonal normal seams.
    value.dx = value.dx * blend - value.height * bz * 6 * tx * (1 - tx) / 8 * std::copysign(1.0f, worldX - ocean->patchCenter.x);
    value.dz = value.dz * blend - value.height * bx * 6 * tz * (1 - tz) / 8 * std::copysign(1.0f, worldZ - ocean->patchCenter.z);
    value.height *= blend;
    value.verticalVelocity *= blend;
    value.velocityX *= blend;
    value.velocityZ *= blend;
    value.accelerationX *= blend;
    value.accelerationZ *= blend;
    return value;
}
const WavePacket* oceanPackets(const Ocean* ocean) { return ocean->packets; }
void              setHullWake(Ocean* ocean, unsigned index, const HullWake& wake)
{
    ASSERT(index < MaxHullWakes);
    ocean->activeHullWakes -= ocean->hullWakes[index].amplitude > 0;
    ocean->activeHullWakes += wake.amplitude > 0;
    ocean->hullWakes[index] = wake;
}
const HullWake*     oceanHullWakes(const Ocean* ocean) { return ocean->hullWakes; }
Vec3                oceanPatchCenter(const Ocean* ocean) { return ocean->patchCenter; }
unsigned            activeWavePackets(const Ocean* ocean) { return ocean->activePackets; }
unsigned            droppedWavePackets(const Ocean* ocean) { return ocean->droppedPackets; }
const SeaState&     seaState(const Ocean* ocean) { return ocean->settings; }
const SpectrumMode* oceanSpectrum(const Ocean* ocean) { return ocean->modes; }
const OceanMetrics& oceanMetrics(const Ocean* ocean) { return ocean->metrics; }
uint32_t            oceanRevision(const Ocean* ocean) { return ocean->revision; }
} // namespace mooring
