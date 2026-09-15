# Water technique evaluation — 15 September 2026

The spectral ocean is a sound foundation. The current result remains below the supplied Sea of Thieves and photographic references.

The largest gaps are coherent breaking events, foam structure, reflection filtering, and local interaction. More FFT samples alone cannot close those gaps. Several effects exist in diagnostic views but contribute weakly, or incorrectly, to beauty.

This evaluation separates published methods, inspected code, observed images, and proposed changes. It does not certify the renderer or the full marina.

## Evidence and scope

The code review covers generation, local wakes, foam transport, particles, optics, sky sampling, geometry, and the physics interface. The latest source revision includes persistent crest events and shared material coordinates for wake foam. Full visual acceptance remains separate from these implementation claims.

The [first source capture](../build/mac/Debug/QA/crest-events-storm-low-v1/MooringSimulator_balanced-storm-00.png) removes the large airborne lace webs. Its [source diagnostic](../build/mac/Debug/QA/crest-events-storm-low-v1/MooringSimulator_balanced-storm-41.png) places events on active compression. The earlier [granular motion sheet](../build/mac/Debug/QA/granular-storm-low-motion/review.png) predates persistent sources and records the random-cluster problem. Neither archive establishes final parity.

The supplied references show broad dark troughs, translucent faces, crest-localized froth, detached spray, and thinner trailing foam. Their lighting and camera angles differ. Those images support a visual target, not a reconstruction of each game's renderer. Motion quality also requires sequences; still images cannot establish it.

## What the sources establish

### WaveWorks 2.0 and ATLAS

WaveWorks 2.0 dates to 2019. NVIDIA describes dual JONSWAP spectra, interactive waves, and anisotropic BRDF data. Our independent wind sea and swell follow the same broad architecture. Our isotropic reflection treatment and limited local disturbances are weaker implementations. [NVIDIA technical overview](https://developer.nvidia.com/blog/three-things-you-need-to-know-about-wave-works-2-0/).

ATLAS separates economical physics from higher visual resolution. Its developers also describe GPU foam, wind-driven spray, and effects that follow surface displacement. This supports our shared physical coefficients and separate visual detail. It also highlights the weak coordination between our current effects. [Developer interview](https://developer.nvidia.com/blog/nvidia-waveworks-2-0-debuts-in-grapeshot-games-atlas/).

The SDK page lists PC/Linux and DirectX/Vulkan, plus a quadtree geometry generator. It does not list Metal. Adopting its design principles fits this project better than replacing Forge with an unsupported SDK integration. [WaveWorks features and platforms](https://developer.nvidia.com/waveworks).

### The three research papers

| Research | Verified capability and cost | Decision for this simulator |
|---|---|---|
| [Water Surface Wavelets, 2018](https://pub.ista.ac.at/group_wojtan/projects/2018_Jeschke_WaterSurfaceWavelets/WaterSurfaceWavelets.pdf) | Spatial, directional amplitude transport supports obstacles. Table 2 reports 8.54 ms for amplitude updates and 4.16 ms for surface evaluation on a GTX 1070. The examples use 16 simulated directions and 120 reconstruction directions. Linear theory excludes breaking splashes. Coarse amplitudes also lose precise phase control. | A candidate for a bounded marina interaction prototype. Preserve phase-coherent wake components for twin-hull interference. It does not replace foam, spray, or an overturning surface. |
| [Wave Curves, 2020](https://pub.ista.ac.at/group_wojtan/projects/2020_Skrivan_WaveCurves/wave_curves_2020.pdf) | Detailed waves evolve on an existing deforming fluid surface. Table 1 gives the breaking-wave example 13.2 seconds for curve simulation and 123 seconds for surface construction per frame. The implementation uses Houdini VEX on an eight-core i7-7820X. | Borrow flow-aligned detail and source selection from fluid motion. The published implementation is an offline reference, not a real-time replacement. These timings are not image-rendering costs alone. |
| [Ships, Splashes, and Waves on a Vast Ocean, 2021](https://computationalsciences.org/publications/huang-2021-vast-ocean/huang-2021-vast-ocean.pdf) | Local FLIP couples to a larger BEM surface. Table 1 reports 42.4 seconds per straight-boat frame and 248.9 seconds for the cycling-boats scene, on different workstation configurations. | Retain the separation of local splashes and propagating waves as a design principle. A direct FLIP/BEM port does not fit our frame or memory budget. Use it as a reference for wake profiles and splash motion. |

Published hardware timings do not predict M3 Max performance. They distinguish demonstrated interactive methods from offline implementations; they do not prove that future GPU adaptations are impossible.

The Omniverse thread is more than an announcement: NVIDIA staff confirmed an Ocean sample in June 2023. Later replies describe limited support. This does not establish a maintained Metal implementation or identify WaveWorks 2.0 itself as a wavelet solver. [NVIDIA discussion](https://forums.developer.nvidia.com/t/using-waveworks-2-0-with-omniverse/241317).

Wave Curves also has an author-published correction for one example. Its project page links the errata. Any implementation must use the corrected formulation. [Author project page](https://visualcomputing.ist.ac.at/publications/2020/WaveCurves/).

### Production water and useful older work

Rare's 2018 note describes FFT water, a scattering approximation, foam feedback, and artist-authored foam textures. Its scattering uses view direction, sunlight, and a crest mask derived from choppiness. The note does not claim full volumetric light transport. Our more elaborate light-depth integration therefore does not imply better-looking water. [The Technical Art of Sea of Thieves](https://history.siggraph.org/wp-content/uploads/2022/09/2018-Talks-Ang_The-Technical-Art-of-Sea-of-Thieves.pdf).

NVIDIA/Gaijin's 2015 presentation separates breaking, turbulent energy, surface stretch, and foam appearance. Compression changes concentration; expansion thins it. This is directly relevant to our tendency to turn one density field into large white patches. [Ocean simulation presentation, slides 13–18](https://developer.download.nvidia.com/assets/gameworks/downloads/regular/events/cgdc15/CGDC2015_ocean_simulation_en.pdf).

Current Crest documentation still uses persistent foam with crest and shallow-water sources. It separates simulation from textured appearance and offers multiscale or stochastic sampling. Persistent foam textures remain useful; their presence alone is not our problem. [Crest foam documentation](https://docs.crest.waveharmonic.com/Manual/Appearance/Foam.html).

Keen Games' 2025 Enshrouded presentation describes sparse surface SDF storage, bounded pools, dirty-region updates, local ripples, and separate underwater/above-water compositing. It explicitly omits large FFT ocean waves. Its RTX 4060 Ti timings include 0.87 ms for reflections and 0.64 ms for volumetrics/refraction/composition at 1440p. These are individual rendering groups, not total fluid simulation costs. The transferable lessons concern resource management, compositing, and profiling. Its stackable water columns solve a different problem from our open ocean. [Presentation, slides 9–10, 18–23, 30–40, 45–46](https://static.graphicsprogrammingconference.com/public/2025/talks/water-simulation-rendering-in-enshrouded/Mantler-Koenen-water-simulation-rendering-in-enshrouded.pdf).

War Thunder's March 2026 overview describes anisotropic reflections, foam microrelief, interaction-driven foam, mesh deformation, and revised particle blending. It publishes no reproducible solver or timing breakdown. These are useful production targets, not enough detail to reproduce its implementation. [Ninth Wave](https://warthunder.com/en/news/9957-development-ninth-wave-new-water-and-naval-battle-effects-en).

Ubisoft's June 2026 account names new water tessellation, volumetric foam, dynamic bubbles, and shared weather variables. It does not disclose the foam representation or water pass costs. Our transported air-depth approximation is not evidence of equivalent volumetric foam. [Black Flag Resynced technical overview](https://blog.playstation.com/2026/06/29/assassins-creed-black-flag-resynced-ps5-pro-enhancements-detailed/).

## Assessment of the current implementation

### Spectrum, geometry, and detail

[Ocean.cpp](/Users/danny/src/mooring-simulator/Examples_3/Unit_Tests/src/40_MooringSimulator/Water/Ocean.cpp) separates wind sea and swell, with finite-depth dispersion. Three physical bands cover 256, 64, and 16 meters. A fourth band supplies normal detail. These choices remain useful.

Both GPU resolutions reconstruct the same 128² physical coefficients. Ultra's 512² transform improves reconstruction sampling; it does not add physical frequencies. Increasing that resolution multiplies storage and transform work without fixing foam sources or reflections.

The radial indexed grid concentrates vertices around the viewer and camera focus. Its budget does not adapt to projected error or visible crests. It also processes regions behind the camera. At low angles, distant geometry loses shape while shading still suggests detail. A bounded, visibility-aware patch grid remains a better LOD experiment than a larger uniform transform.

### Reflections and scattering

[Water.frag.fsl](../Examples_3/Unit_Tests/src/40_MooringSimulator/Shaders/Water.frag.fsl) collects mean slopes and a scalar second moment. Unresolved variance now broadens both the sun highlight and filtered sky reflection. Spherical-area weights preserve the sky's mean radiance. Object reflection still traces one direction, and directional slope covariance remains absent.

This closes part of the geometry-to-normal-to-BRDF transition. It remains an isotropic approximation of the richer treatment in [Bruneton, Neyret, and Holzschuch](https://evasion.inrialpes.fr/Publications/2010/BNH10/article.pdf). Mean environment Fresnel uses the paper’s equation 26 with scalar slope variance. Magnified visible sky uses positive cubic reconstruction. A three-scale HDR bloom adds restrained glow around bright highlights, including calm-water sun glints.

The current light-entry pass correlates scattering with displaced geometry and sunlight. It uses two to six interior samples, a flat-water baseline, and an artistic contrast multiplier. It omits refraction at sunlight entry and multiple scattering. A diffuse-sky approximation now shares the measured path under overcast conditions; it does not integrate the full hemisphere.

The next optical comparison must hold geometry fixed while it changes sun angle, roughness, or cloud cover separately. Brightness must follow those changes consistently. A low camera must not be necessary to conceal weak wave shapes at the normal overview angle.

The new fixed Whitecaps views separate environment reflection, water-body light, and mean Fresnel before foam and fog. They place the broad silver ribbons mainly in environment reflection. Water-body light already follows crest shape. A combined-deformation normal experiment added 10.67 MiB in Ultra without a clear visual benefit; it was removed. Increasing transform storage did not solve this optical gap.

### Breaking, foam, and particles

[Foam shading](../Examples_3/Unit_Tests/src/40_MooringSimulator/Shaders/Water.frag.fsl) separates transported density from textured coverage. Age and flow control erosion. Subpixel texture coverage converges toward average density, which fixes the former distant white islands. Fine pores remain visible inside fresh sheets. Raw density checks still cannot certify beauty.

Half-precision moments transport foam age, air concentration, and air depth beside density. Instantaneous breaking stays separate. Local flow includes a bounded divergence correction. Spectral shading approximates surface concentration from filtered band stretch and slope. The product neglects cross-band shear. Rasterized triangle area caused camera-dependent facets and was removed. The two corrections are applied separately.

[Crest emission](../Examples_3/Unit_Tests/src/40_MooringSimulator/Shaders/WaterEffects.comp.fsl) tracks 256 sources in fixed 4 m world cells. Each follows active compression for at most 2.5 seconds. Compression controls emission; orbital velocity controls launch. Each event receives one, four, or eight particle slots by quality. Balanced and Ultra reserve one for replenished crest froth. Calm and paused-resize checks distinguish these events from camera-dependent random emission.

Compact raised crowns and wake rafts conform to the wave at each vertex. Wake rafts share the surface's material field and advection phases. They no longer rotate one texture stamp per particle. Older rafts settle quickly and develop pores. Stronger stern injection and fewer rafts shift visible wake foam into the transported field.

Measured launch velocity scales crest froth size. The principal surface-stretch direction aligns new parcels along the breaking lip. Sampled foam mass controls crown height, and shared textured thickness retains porous interiors. Fine pores use world scale. Shared lighting and hull/wave shadows keep raised parcels consistent with the surface. Fresh foam uses independently filtered cluster scales and optical coverage to retain detail inside dense caps.

Raised froth now also loses relief and opacity when its tracked breaking source passes. Transported surface foam remains underneath. Rain expires on contact instead of creating small foam rafts. Both changes reuse existing particle storage.

Filtered procedural droplets replace the former stretched lace cards. Airborne parcels share one ballistic state, while mist has wind drag and a short lifetime. Directional scattering and shared shadows now affect airborne brightness. This is a bounded source model, not a resolved overturning surface or continuous breaking-front solver. The 4,096-slot pool remains fixed.

[Bubble scattering](../Examples_3/Unit_Tests/src/40_MooringSimulator/Shaders/Water.frag.fsl) uses transported concentration and variable depth. Injection, rise, and dissolution replace the fixed 45 cm layer. The representation is one depth column per surface sample. It does not simulate independent three-dimensional bubbles or liquid sheets.

### Hulls, docks, and physics

The attached pressure profile creates bow pile-up, shoulder drawdown, and stern recovery. Nine outgoing packet directions approximate a continuous wake. The shared CPU/GPU gradients and resistance-energy budget are useful foundations. Their calibration remains empirical.

[Dock reflection](/Users/danny/src/mooring-simulator/Examples_3/Unit_Tests/src/40_MooringSimulator/Water/Ocean.cpp:369) tests packet crossings against two fixed dock faces. Background-spectrum injection is absent. The dock therefore cannot produce a complete wave shadow or diffraction field. Extra contact foam cannot compensate for that missing interaction.

Two scaling limits need correction before the integrated marina. [Wake energy and time](/Users/danny/src/mooring-simulator/Examples_3/Unit_Tests/src/40_MooringSimulator/Simulation/Simulation.cpp:67) reside in `World` but update inside the vessel loop. Multiple vessels would share their emission budget and clock. [Effect preparation](/Users/danny/src/mooring-simulator/Examples_3/Unit_Tests/src/40_MooringSimulator/Water/OceanRenderer.cpp:262) accepts one vessel snapshot and layout. Existing capacity constants do not establish sixteen-vessel visual support.

Jolt remains responsible for vessel motion. Changes to visual quality must preserve the physical spectrum, collision behavior, and scoring. New interactive waves need a consistent force-query path. A GPU-only obstacle field with no physics representation cannot satisfy that requirement.

## Implementation status and acceptance

| Area | Current change | Acceptance still required |
|---|---|---|
| Foam lifecycle | Transported age, active breaking, air concentration, and depth; filtered coverage | Beauty and motion must show fresh crests, porous remnants, and clear troughs. |
| Crest events | Persistent sources, wave-driven launch, compact froth and granular spray | Low and overview sequences must connect shedding to the same crest over time. |
| Reflections | Scalar-variance sky filtering and spherical energy checks | Object roughness and directional covariance remain incomplete. |
| Hull effects | Per-hull pressure profile, packet waves, transported aeration | Turning, reversal, and twin-hull beauty must retain readable displacement. |
| Local interaction | Existing wave packets and paired-field cancellation | Background injection and an obstacle-aware amplitude prototype remain unimplemented. |
| Performance | Opt-in independent Forge timings; fixed resource pools | Clean beauty runs, resize, and quality reloads must meet device budgets. |

Full FLIP/BEM integration, full Wave Curves reconstruction, and unlimited volumetric foam are deferred. Their published implementations do not fit this prototype's demonstrated budget. Art-directed sheets and a shallow aeration model still need honest names and visual validation.

## Performance, memory, and code discipline

The renderer now supports `--water-profile` for six independent Forge GPU queries: FFT, effects, sky, geometry, surface, and post. Metal supports one active stage query, so these scopes never nest. Profiling allocates its own resources only when requested.

The reported GPU values sum encoder stage durations. They are not elapsed frame time and can overlap. Frame interval remains a separate measure. The [QA record](WATER_QA.md) holds run-specific timings, resource totals, and validation results. Screenshot runs cannot establish sustained performance.

The implementation uses Forge resources, FSL tables, mapped constants, bounded pools, half conversion, and allocation hooks. Scalar standard-library math needs no replacement wrapper. Shared surface queries serve particle motion and froth geometry. Dead particle vertices exit before expensive surface queries. Post-processing reuses bloom lighting across its FXAA taps.

The crest state costs 8 KiB. Compact whitewater history costs 3.67 MiB in Balanced or 11.67 MiB in Ultra. Sky filter levels cost 1.33 or 5.33 MiB respectively. HDR bloom costs about 1.44 MiB at 1920 × 1200. Resource bytes occupy GPU heaps; they are not additional allocations beyond those heaps.

Stateless normal and local-displacement buffers now share storage across frames on the ordered graphics queue. Transient depth tests use Forge on-tile targets on Apple silicon. Together these changes remove 99.89 MiB of Ultra resource storage. The transient-depth comparison preserves all 200 pixel arrays. Startup still reserves 608 MiB of heaps; resize and quality reload settle at 512 MiB.

The 15 September performance pass adds contiguous CPU FFT traversal, shared-memory GPU transforms, compact whitewater history, conservative shadow rejection, and hardware sky filtering. It removes another 26.67 MiB of Ultra resources. Uncapped fixed-workload frame intervals fall from about 15.3 to 8.3 ms in Balanced and 16.7 to 14.6 ms in Ultra. The [QA record](WATER_QA.md#performance-preservation-pass-15-september) documents the captures, rounding differences, tests, and measurement limits.

Remaining budget checks include mobile devices and sixteen vessels. Startup still exceeds the proposed 512 MiB mobile budget. Zero fixed-step allocations in a single-vessel fixture do not prove future capacity. Surface fields retain manual buffer sampling because normal-variance filtering differs from ordinary hardware interpolation.

Quality acceptance depends on beauty and motion alongside numerical checks. A feature list or nonempty diagnostic buffer is insufficient.
