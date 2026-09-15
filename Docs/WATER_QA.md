# Water and physics review — 15 September 2026

**The beauty pass remains under active review.** The latest storm has steeper wave faces, darker troughs, and less lingering foam. Shared material coordinates remove the rotated wake stamps. Fresh foam now retains smaller clustered detail and shares hull and wave shadows with the water. Whitecaps still has broad silvery reflections. Glass retains its low-ripple response.

Earlier captures showed fixes for foam bands, repeated shadows, geometric ripples, and refraction outlines. Those checks do not approve later changes or every camera angle. Numerical validation and nonempty effect buffers establish function, not visual quality.

The [technique evaluation](WATER_TECHNIQUE_EVALUATION.md) records code-level gaps, primary sources, and the next acceptance steps. The nine-direction wake and attached pressure profile remain approximations. The prototype boat and marina geometry also limit the overall comparison.

## Reproduce the captures

Build the native application using [MOORING.md](../MOORING.md). Then run:

```sh
xcodebuildmcp macos launch --json '{"appPath":"/Users/danny/src/mooring-simulator/build/mac/Debug/MooringSimulator.app","launchArgs":["--water-qa-ultra"]}' --output text
```

Use `--water-qa` for Balanced. Add `--water-qa-scene=N` to run one scene. An explicit scene takes precedence over mode defaults, regardless of argument order. The application exits after capture.

Add `--water-qa-motion` to capture twelve seconds from that scene at 15 fps. Add `--water-qa-seconds=24` for a longer sequence. The supported duration is 1–60 seconds. The simulation still advances at 30 Hz with 60 Hz physics.

Add `--water-qa-low` for a 12° camera elevation. Use `--water-qa-waterline` to keep the camera 1.2 m above its local wave. These options use the existing continuous camera.

Use `--water-qa-still` for thirty paused captures. With either option, `--water-qa-inspect=N` selects a diagnostic view. `--water-qa-sun=N` and `--water-qa-sun-height=N` set sun azimuth and elevation in degrees. These controls support comparisons with fixed wave geometry.

Add `--water-qa-rudder=N` to Wake or Catamaran for a turn, with rudder angle in degrees. This supports the repeated-foam-pattern review during manoeuvres, beyond straight travel.

`--water-qa-resize` changes the native window through five sizes and captures beauty, reflection, refraction, and mesh lighting. `--water-qa-reload` cycles Low, Balanced, and Ultra three times.

| N | Scene | Purpose |
|---|---|---|
| 0 | Calm | Overview, shadows, absorption, and sun glints |
| 1 | Rough | Near-surface geometry, normal detail, and LOD |
| 2 | Storm | Foam, rain, spray, scattering, and vessel motion |
| 3 | Wake | Twenty seconds of powered monohull travel |
| 4 | Dock | Shallow water, intersections, shadow, and refraction |
| 5 | Light | Low sun, clouds, atmospheric rays, and fog |
| 6 | Calm at water level | Gentle swell and reflection breakup |
| 7 | Glass at water level | The latest calm-water photograph reference |
| 8 | Whitecaps | Breaking crests, foam, and spray |
| 9 | Motion | Thirty consecutive captures while the camera rotates |
| 10 | Catamaran | Twenty seconds with twin engines and separated hull trails |

Files are written to `build/mac/Debug/QA`. Each filename includes the quality, scene, and effect number. The captures use fixed steps and seeds. Quality changes preserve physical coefficients.

Generate review sheets with Python and Pillow available:

```sh
python3 Tools/Mooring/check_water_captures.py build/mac/Debug/QA --preset ultra
python3 Tools/Mooring/check_water_captures.py build/mac/Debug/QA --preset balanced
```

The motion check detects large brightness discontinuities. It cannot certify temporal stability or visual quality. Inspect its contact sheet, all eight effect sheets, and the beauty overview as well. Older 42-view archives retain seven effect sheets.

### Intermediate-frame corruption, 15 September

Continuous [Tracy recording](TRACY.md) reproduced torn ocean geometry between saved screenshots, including the orange and purple foam-age view.
The render-pass boundary could consume a pending fence without publishing a new fence before the next frame reused water buffers.
Explicit pass termination now publishes the fence. Optional Tracy thumbnails also participate in that fence chain.
The correction adds no CPU wait for GPU completion and changes no wave equations or materials.

The [regression check](../Tools/Mooring/check_tracy_frames.py) compares 522 frames across 174 paused diagnostic sequences.
It allows an image-average difference of 0.01 out of 255 for quantized sky-filter convergence.
The corrupted recording fails this check. Corrected Ultra and Balanced recordings pass, with maximum differences of 0.0011 and 0.0047.
All 24 checked beauty images remain pixel-identical to the pre-instrumentation references.
Five window sizes and nine graphics reloads also pass the capture checks.

- [Continuous-frame result](../build/tracy-fixed-ultra-stability.json)
- [Balanced continuous-frame result](../build/tracy-fixed-balanced-stability.json)
- [Beauty comparison](../build/tracy-fixed-beauty-comparison.json)
- [Balanced beauty comparison](../build/tracy-fixed-balanced-beauty-comparison.json)
- [Corrected foam-age capture](../build/mac-tracy/Debug/QA/tracy-validated-ultra/MooringSimulator_ultra-storm-35.png)

Synchronous screenshot readback waits for rendering and can hide this class of fault. Retain intermediate frames when reviewing synchronization changes.

The completed `connected-final` runs contain 200 captures per quality, including all 42 diagnostic views and thirty camera-motion frames. Both use the same source build. Their seven effect sheets, camera-motion sheets, and ten-scene beauty sheets were visually inspected. These captures include porous fresh foam, shared wake coordinates, strain-aligned crowns, finer spray, and directional particle lighting.

- Beauty overview: [Ultra](../build/mac/Debug/QA/connected-final-ultra/ultra-beauty-review.png), [Balanced](../build/mac/Debug/QA/connected-final-balanced/balanced-beauty-review.png).
- Ultra: [whitecaps](../build/mac/Debug/QA/connected-final-ultra/MooringSimulator_ultra-whitecaps-00.png), [storm](../build/mac/Debug/QA/connected-final-ultra/MooringSimulator_ultra-storm-00.png), [wake](../build/mac/Debug/QA/connected-final-ultra/MooringSimulator_ultra-wake-00.png), [catamaran](../build/mac/Debug/QA/connected-final-ultra/MooringSimulator_ultra-catamaran-00.png).
- Ultra effects: [0–5](../build/mac/Debug/QA/connected-final-ultra/ultra-effects-0.png), [6–11](../build/mac/Debug/QA/connected-final-ultra/ultra-effects-1.png), [12–17](../build/mac/Debug/QA/connected-final-ultra/ultra-effects-2.png), [18–23](../build/mac/Debug/QA/connected-final-ultra/ultra-effects-3.png), [24–29](../build/mac/Debug/QA/connected-final-ultra/ultra-effects-4.png), [30–35](../build/mac/Debug/QA/connected-final-ultra/ultra-effects-5.png), [36–41](../build/mac/Debug/QA/connected-final-ultra/ultra-effects-6.png).
- Balanced effects: [0–5](../build/mac/Debug/QA/connected-final-balanced/balanced-effects-0.png), [6–11](../build/mac/Debug/QA/connected-final-balanced/balanced-effects-1.png), [12–17](../build/mac/Debug/QA/connected-final-balanced/balanced-effects-2.png), [18–23](../build/mac/Debug/QA/connected-final-balanced/balanced-effects-3.png), [24–29](../build/mac/Debug/QA/connected-final-balanced/balanced-effects-4.png), [30–35](../build/mac/Debug/QA/connected-final-balanced/balanced-effects-5.png), [36–41](../build/mac/Debug/QA/connected-final-balanced/balanced-effects-6.png).
- Camera motion: [Ultra](../build/mac/Debug/QA/connected-final-ultra/ultra-motion-review.png), [Balanced](../build/mac/Debug/QA/connected-final-balanced/balanced-motion-review.png). The maximum adjacent-frame channel-mean changes were 1.21 and 0.99 out of 255. This measures coarse discontinuities, not temporal quality.
- Logs: [Ultra](../build/qa-connected-final-ultra.log), [Balanced](../build/qa-connected-final-balanced.log). [Source hashes](../build/qa-connected-final-source.sha256) identify the reviewed application and shaders.

Named archives preserve evidence. Files directly in `QA` are replaced by later runs and must not serve as immutable review links.

The earlier `complete-residency-ultra` run and its repeat match in all 200 pixel arrays. Their seven effect sheets and camera-motion sheet were inspected. Before that fix, identical-binary runs differed in paused diagnostic views. Complete descriptor updates fixed the missing Metal resource declarations without adding a queue stall. [Repeat comparison](../build/qa-complete-residency-repeat-comparison.txt), [first log](../build/qa-complete-residency-ultra.log), [repeat log](../build/qa-complete-residency-repeat.log).

The final low-angle storm run contains 180 frames over twelve simulated seconds. Its [one-second review sheet](../build/mac/Debug/QA/connected-final-storm-motion/motion-review.png) and full-resolution samples were inspected. The sequence retains dark troughs and shows compact spray above active crests. Broad fresh sheets and isolated foam flecks remain visible. The [15 fps video](../build/mac/Debug/QA/connected-final-storm-motion/storm-motion.mp4) is available for continuous playback; sampled-frame review does not certify all temporal details. [Run log](../build/qa-connected-final-storm-motion.log).

The [fixed low-sun comparison](../build/mac/Debug/QA/directional-spray-low-sun/lighting-review.png) isolates the directional spray-lighting change in beauty, airborne spray, and mist. The effect remains restrained under cloud occlusion. It does not resolve the broad silver reflections elsewhere in the water.

The Breeze fixture also advanced during explicit still capture and double-stepped part of explicit motion capture. Its advance guard now excludes both modes. Thirty corrected paused density captures are pixel-identical. [Paused check](../build/qa-breeze-frozen-density.log).

A Gaussian-slope sun model was evaluated in fixed Calm and Whitecaps scenes. It sharpened calm highlights but did not resolve the broad silver sky reflections. The experiment was removed; the renderer retains Forge GGX and Schlick helpers. A three-sample wave-horizon experiment also failed to resolve the broad reflections. It was removed before performance acceptance, so it adds no runtime cost.

### Follow-up appearance pass, 15 September

The performance checkpoint is preserved separately from these intentional appearance changes. Rain now expires on water contact without making foam rafts. Raised crest froth loses relief and opacity when its tracked breaking source passes. Transported foam remains underneath. Larger cloud billows and stronger broad grouping reuse the existing texture and samples. None of these changes adds a render pass, particle slot, or GPU resource.

The [optical split](../build/mac/Debug/QA/refinement-ultra/ultra-effects-7.png) isolates the remaining silver ribbons in environment reflection. Water-body light already follows the wave shape. A combined-deformation normal experiment added 10.67 MiB in Ultra without a clear visual benefit, so it was removed. Broad reflections, isolated foam flecks, and repeated cloud silhouettes remain below the reference quality.

The final fixed suites contain 204 captures per quality. All eight effect sheets, both camera-motion sheets, and both beauty overviews were inspected. Startup checks pass in both runs. The Breeze discontinuity measures are 1.27/255 in Ultra and 1.02/255 in Balanced. [Ultra overview](../build/mac/Debug/QA/refinement-ultra/ultra-beauty-review.png), [Balanced overview](../build/mac/Debug/QA/refinement-balanced/balanced-beauty-review.png), [Ultra log](../build/qa-refinement-ultra.log), [Balanced log](../build/qa-refinement-balanced.log), [source hashes](../build/qa-refinement-source.sha256).

Four Ultra motion archives each contain 360 frames over 24 simulated seconds. Contact sheets and selected full-resolution adjacent frames were inspected. Videos support continuous playback; sampled review does not certify every temporal detail.

| Sequence | Review finding | Evidence |
|---|---|---|
| Calm, low camera | Gentle swell and coherent reflection breakup remain. Maximum adjacent-frame channel-mean change: 0.59/255. | [Sheet](../build/mac/Debug/QA/refinement-calm-motion/motion-review.png), [video](../build/mac/Debug/QA/refinement-calm-motion/calm-motion.mp4) |
| Whitecaps, waterline | Successive crests retain dark troughs, transmitted colour, and sparse shedding. After 17 seconds, the following camera reaches the dock. Its foreground occlusion limits the water review and causes the largest mean change, 26.69/255. | [Sheet](../build/mac/Debug/QA/refinement-whitecaps-motion/motion-review.png), [video](../build/mac/Debug/QA/refinement-whitecaps-motion/whitecaps-motion.mp4) |
| Storm, low camera | Raised froth settles after active crests. The largest mean change, 11.12/255, occurs as a nearby foamy crest crosses the foreground; its adjacent frames remain coherent. | [Sheet](../build/mac/Debug/QA/breaker-storm-motion/motion-review.png), [adjacent frames](../build/mac/Debug/QA/breaker-storm-motion/largest-changes.png), [video](../build/mac/Debug/QA/breaker-storm-motion/storm-motion.mp4) |
| Wake, 20° rudder | Foam follows the curved trail without the former rotated branches. Maximum adjacent-frame channel-mean change: 0.58/255. | [Sheet](../build/mac/Debug/QA/refinement-turning-wake/motion-review.png), [video](../build/mac/Debug/QA/refinement-turning-wake/wake-motion.mp4) |

These motion statistics describe the bottom third of reduced frames. They differ from the fixed Breeze test region and cannot use its 8/255 threshold. The dock example shows why image review is required.

## Effect inspection

The suite has 45 views. Diagnostic masks show intermediate values, which can differ from the final textured or shaded contribution. Black is valid when an effect has no source.

| View | Evidence and interpretation |
|---|---|
| 0 — Beauty | Calm and glass preserve restrained ripples. Steeper storm faces improve volume. Whitecaps remains too silvery. Fresh caps retain texture, but broad creamy films and isolated flecks still limit the reference match. |
| 1 — LOD | Continuous radial mesh density and filtered shading reach the horizon without the former rectangular band. |
| 2 — Combined foam | Erosion changes coverage. Faster post-break decay clears troughs. Stronger stern injection now supplies more of the visible wake trail. |
| 3 — Normals | Broad waves and fine detail remain separate. Calm does not carry Rough's ripple energy. |
| 4 — FFT foam | Calm is empty. Whitecaps and Storm contain crest sources and decaying foam. This view shows raw density, without a hidden multiplier. |
| 5 — Local foam | Wave velocity and curl eddies transport density. Convergence and divergence change concentration. The fine foam texture is applied later. |
| 6 — Contact foam | Thin bands follow the hull and dock at the waterline. These are intersection highlights, not a complete shoreline simulation. |
| 7 — Shadows | Filtered hull and dock shadows remain continuous. Water light-entry depth also shadows direct glints and foam behind crests. The object shadow region spans 64 m. |
| 8 — Reflection | Rays follow displaced water normals through the mirrored depth capture. Off-screen and occluded geometry remain unavailable. |
| 9 — Optical depth | Shallow contact regions differ from open water. Depth remains continuous across the visible flat seabed. |
| 10 — Wave volume scattering | Light-entry depth and refracted view paths control turquoise scattering. Holding waves fixed and changing the sun changes the lit faces. |
| 11 — Refraction | The former bright expanded hull and rail outlines are absent. An analytic flat-bottom fallback removes screen-trace gaps. |
| 12 — Sun glints | The finite sun produces irregular highlights without the earlier crossed geometric lines. |
| 13 — Underwater rays | Hull and dock occlusion changes the integrated underwater light. No caustic pattern is claimed. |
| 14 — Cloud shadows | Low-sun occlusion is visible. Some views cover an almost uniformly shaded region because the cloud scale exceeds the local marina. |
| 15 — Local displacement | Each hull creates bow pile-up, shoulder drawdown, and stern recovery. Nine outgoing directions form additional crests and troughs. |
| 16 — Compression | Compressing regions follow displaced crests. Minimum surface stretch controls calibrated foam production. |
| 17 — Ripples | Wind supplies an independent short-wave normal band. Filtering removes unresolved detail toward the horizon. |
| 18 — Spray | White droplets, blue parcels, orange mist, green rafts, and pink froth use beauty-pass sizes. Persistent sources replace camera-dependent placement. |
| 19 — Rain | Wind-driven streaks cross storm water. Particle depth prevents foreground leaks. |
| 20 — Atmospheric rays | Cloud gaps produce directional shafts. Quarter-resolution integration and depth-aware filtering reduce grain. The view multiplies radiance by six. |
| 21 — Fog | Distance attenuation approaches the horizon continuously. |
| 22 — FFT displacement | Crest and trough heights remain continuous across the spectral surface. |
| 23 — Local normals | Wake gradients agree with local displacement. This view multiplies slopes by eight. |
| 24 — Sky | Cached volume clouds supply reflected radiance. Broad weather variation breaks repeated cloud groups; shape remains a prototype. |
| 25 — Cloud opacity | Cloud gaps and the distant horizon fade are visible separately from lighting. |
| 26 — Displaced mesh | Direct lighting of rasterized triangles shows real wave shape without foam or shading normals. Distant facets expose the geometry budget rather than concealing it. |
| 27 — Reflection coverage | Accepted ray hits expose missing or discontinuous object coverage separately from reflected colour. |
| 28 — Foam relief | Surface foam shading normals reveal texture relief. This is separate from raised particle geometry. |
| 29 — Foam transport | Colour shows flow; intensity shows local concentration. Global current remains valid outside the local field. |
| 30 — Cloud noise | A slice exposes the cached shape and detail fields. |
| 31 — Cloud section | Density through the cloud layer exposes shape and vertical limits without lighting. |
| 32 — Water light path | White represents a mean 12 m path from the illuminated wave surface. |
| 33 — Under-foam scattering | Transported air concentration and depth light aerated water. This is a depth-column approximation, not a resolved bubble volume. |
| 34 — Active breaking | Instantaneous sources follow minimum surface stretch. They remain separate from foam history. |
| 35 — Foam age | New and old mass remain distinguishable during transport. |
| 36 — Air and depth | Plunge-driven injection, rise, and dissolution evolve independently of surface coverage. |
| 37 — Highlight bloom | Three HDR scales soften bright water glints. Dark water contributes no bloom. |
| 38 — Airborne spray material | Compact droplets and granular parcels replace airborne lace cards. Coverage remains sparse. |
| 39 — Raised foam material | Wave-following crowns provide relief. Wake rafts share the water material instead of rotating one stamp per particle. |
| 40 — Mist material | Soft, short-lived parcels provide restrained haze above active sources. |
| 41 — Crest sources | Emission and event age show tracked breaking locations. Calm and pause checks keep these sources inactive. |
| 42 — Environment reflection | Linear sky-reflection contribution, including mean Fresnel, before foam and fog. |
| 43 — Water body | Linear transmitted and scattered water contribution, including its Fresnel weight, before foam and fog. |
| 44 — Environment Fresnel | Mean reflection weight, including unresolved slope variance. |

The references organize cream foam around breaking lips and retain broad dark troughs between them. The current build improves that hierarchy but does not reproduce it consistently. Breaking surf remains outside the PoC. The requested storm whitecaps, visible wake deformation, and convincing crest froth remain required quality work.

## Physics and numerical checks

The current CPU regression executables pass with zero failures and no tracked leaks. Logs: [ocean](../build/tests-ocean-performance.log), [physics](../build/tests-physics-performance.log), [simulation](../build/tests-core-performance.log). The later appearance changes do not modify CPU simulation code.

- `MooringOceanTests`: direct DFT comparison, spectral energy, seeds, finite depth, current, orbital motion, packet continuity, source ownership, and long water runs.
- `MooringPhysicsTests`: hydrostatic balance, restoring moments, decay, coast, reverse, propeller wash, steering, differential thrust, dock collision, and grounding.
- `MooringTests`: physical helm access, crew eligibility, travel, preserved controls, bounded commands, resets, powered turns, and a ten-minute run.

The application also checks GPU transforms, slope-moment mips, local reconstruction, calm foam, storm coverage, and particle production. The hull test requires droplets, foam, and sheets after five seconds, plus measurable airborne height. Startup readback is confined to validation. Ordinary CPU water queries do not read GPU resources.

The current GPU checks reject foam drift that loses the global current outside the local wake field. Storm coverage is measured across the three raw cascade fields; it is not a percentage of visible white pixels.

New CPU checks cover separated catamaran bow waves, drawdown, reverse direction, source exclusion, and analytic wake gradients. Both boat configurations must produce a measurable crest and trough after powered travel.

The latest physics regression gives these outcomes. They describe the model, not sea-trial measurements:

| Scenario | Oceanis 40.1 | Lagoon 42 |
|---|---:|---:|
| Ahead at 70% throttle | 4.39 kn | 5.19 kn |
| Reverse test, final speed | 2.49 kn | 3.20 kn |
| Wake, trough / crest | −0.126 / 0.248 m | −0.173 / 0.348 m |
| Storm, peak roll | 29.92° | 13.06° |
| Storm, peak heave | 2.09 m | 2.06 m |

Lagoon differential thrust produces 42.58° of heading change in ten seconds. These are regression baselines. Factory righting curves, detailed hull offsets, propeller curves, and sea trials are still needed for training validation.

## Performance and memory

The captures use an Apple M3 Max, macOS 26.6.2, and Debug builds at 1920 × 1200. Screenshot timings do not establish sustained performance. Separate runs below measure beauty without capture work.

### Performance preservation pass, 15 September

The pass preserves spectrum resolution, sample counts, particles, physics steps, and normal playback. The opt-in `--benchmark-unthrottled` flag removes VSync and uses a 1 ms AppKit timer. Ordinary playback retains its 30 Hz timer. QA advances two 60 Hz physics steps per rendered frame, even when uncapped. These measurements therefore describe that fixed workload, not real-time simulation cost at each reported frame rate.

| Measurement | Before | After |
|---|---:|---:|
| Balanced storm frame interval | 15.23–15.47 ms | 8.32–8.34 ms |
| Ultra storm frame interval | 16.64–16.86 ms | 14.54–14.65 ms |
| Standalone CPU ocean update | 6.920 ms | 1.985 ms |
| Ultra live GPU resources | 319.39 MiB | 292.71 MiB |
| Balanced live GPU resources | 150.53 MiB | 143.85 MiB |

Frame ranges exclude the first timing window. The standalone baseline briefly overlapped a build, so its precise ratio is approximate. Independent application CPU timings also confirm the reduction. [Baseline Ultra](../build/qa-perf-baseline-ultra.log), [baseline Balanced](../build/qa-perf-baseline-balanced.log), [optimized Ultra](../build/qa-perf-sky-texture-ultra.log), [optimized Balanced](../build/qa-perf-final-balanced.log), [CPU test](../build/tests-ocean-performance.log).

The retained changes are:

- CPU Stockham butterflies traverse contiguous memory on both axes. Zero-energy modes skip phase evaluation. Arithmetic and physical coefficients remain unchanged.
- GPU transforms keep intermediate stages in 16 KiB of group memory. Both axes reuse one global buffer. Known zero spectrum rows skip horizontal transforms.
- Whitewater history stores only its two active bands. This and the FFT buffer change remove 26.67 MiB in Ultra and 6.67 MiB in Balanced.
- Conservative caster bounds skip shadow taps only where their result is known. Opaque scene pixels skip hidden sky reconstruction.
- A Forge RGBA16F texture stores the sky and its spherical-area mip chain. Four hardware bilinear reads evaluate the existing positive cubic filter. Parent and destination mip views use UAV access during filtering. Readback uses Forge's texture-copy helper with corrected mip dimensions.

The first FFT comparison preserves all 200 pixel arrays. Compact history and shadow bounds change only 60 pixels across four captures, by at most two channel values. Hardware filtering introduces rounding differences: 148 of 200 Ultra captures and 150 of 200 Balanced captures remain exact. Worst image-average channel differences are 0.068 and 0.071 out of 255. Isolated antialiasing decisions cause larger peak differences around tiny highlights. Beauty overviews and a magnified worst-case comparison show no visible appearance regression. This is visual preservation, not bitwise equality. [Ultra comparison](../build/qa-perf-texture-equality-comparison.txt), [Balanced comparison](../build/qa-perf-final-balanced-comparison.txt), [highlight comparison](../build/mac/Debug/QA/perf-texture-equality/highlight-comparison.png).

The final UAV mip-view cleanup preserves all 200 captures from the hardware-filtering checkpoint. [Final comparison](../build/qa-performance-final-comparison.txt). A separate storm resize restores all three diagnostic captures exactly. Restored beauty differs in 17 pixels, by at most one channel value. [Storm resize comparison](../build/qa-perf-final-storm-resize-comparison.txt).

All three rebuilt CPU regression suites pass, with no tracked leaks. GPU startup checks pass for transforms, transport, particles, cloud resets, and spherical radiance. Dock resize restores reflection, refraction, and mesh lighting exactly. Beauty differs by one channel value in one pixel. Nine quality reloads return to stable resource totals. [Physics](../build/tests-physics-performance.log), [gameplay](../build/tests-core-performance.log), [resize](../build/qa-perf-final-resize.log), [resize comparison](../build/qa-perf-final-resize-comparison.txt), [reload](../build/qa-perf-final-reload.log).

Startup still reserves 608 MiB of Ultra heaps; resize and reload settle at 512 MiB. These changes reduce live resources, not that retained heap ceiling. Fixed-step allocation and packet-overflow counters remain zero. Mobile and multiple-vessel workloads remain unmeasured. [Source hashes](../build/qa-performance-source.sha256) identify this performance checkpoint.

Separate 1,200-frame runs measure the final appearance changes without concurrent captures, encoding, builds, or tests. Balanced remains at 8.33 ms per frame; Ultra measures 13.78–13.88 ms after its first timing window. Live resources remain 143.85 and 292.71 MiB, respectively. Both retain zero simulation allocations and packet overflows. These runs preserve the measured gains; their timing difference does not isolate the cost of any single visual change. [Final Balanced](../build/qa-refinement-benchmark-balanced.log), [final Ultra](../build/qa-refinement-benchmark-ultra.log).

### Earlier measurements, 14 September

The preceding Ultra startup reported 319.39 MiB of GPU resources in 608 MiB of heaps, including 153.47 MiB of ocean buffers. Sharing stateless normal and local-displacement buffers removed 22.33 MiB. Forge on-tile depth tests removed another 77.56 MiB. All 200 captures matched before and after the depth-storage change. [Pixel comparison](../build/qa-transient-depth-comparison.txt).

Settled tracked CPU memory is about 24.77 MiB. Resource bytes occupy the heaps; do not add both totals. Ultra startup still exceeds the proposed 512 MiB mobile heap budget, although resize and quality reload settle at 512 MiB. This desktop result does not establish a mobile budget.

The CPU ocean regression reports 3.14 MiB tracked storage and a maximum FFT reference error of 0.00000048 m. Its 7.105 ms update timing overlaps graphics work and is not an isolated benchmark.

`--water-benchmark=N` runs beauty without screenshots after warmup. `--water-profile` enables six independent Forge queries: FFT, effects, sky, geometry, surface, and post. Metal stage durations can overlap. They measure encoder work, not elapsed frame time. Frame interval is reported separately. Profiling adds about 12 MiB of tracked CPU storage and is disabled by default.

Current GPU startup checks pass for FFT agreement, mips, packet gradients, calm isolation, whitewater transport, persistent crest sources, particle launch, cloud history, and paused fields. The Ultra storm fixture measures 19.81% raw cascade coverage above density 0.05 and a 1.307 m maximum air-depth moment. This is not screen-space foam coverage. Powered catamaran spray reaches 0.518 m above the sampled water.

The current build completed separate 900-frame storm benchmarks without captures, builds, or CPU tests running alongside it. Both held 33.33–33.36 ms frame intervals at the application's 30 Hz cap. This does not establish 60 fps. The measurements include the final shared foam, directional spray lighting, complete descriptor bindings, and transient depth targets.

| Encoder group | Balanced range (ms) | Ultra range (ms) |
|---|---:|---:|
| FFT | 0.28–0.54 | 1.22–1.30 |
| Effects | 0.09–0.18 | 0.09 |
| Sky | 0.42–0.88 | 2.10–2.14 |
| Geometry | 0.19–0.40 | 0.44 |
| Surface | 4.84–8.22 | 10.72–11.00 |
| Post | 1.26–1.55 | 2.10–2.11 |

Logs: [Balanced storm](../build/qa-connected-final-benchmark-balanced.log), [Ultra storm](../build/qa-connected-final-benchmark-ultra.log). Encoder durations can overlap; do not add this table into an elapsed-frame claim. Balanced started with 224 MiB of heaps. The larger 384 MiB retained after quality cycling reflects allocator history, not additional live resources.

The [current native resize review](../build/mac/Debug/QA/current-resize/resize-review.png) covers landscape, portrait, and odd requested sizes. macOS snapped odd sizes to even drawable dimensions. Restored reflection, refraction, and mesh-lighting captures are pixel-identical. Beauty differs by one channel value in nine pixels. GPU resources return to 319.39 MiB; the allocator retains 512 MiB of heaps after the larger window. [Log](../build/qa-current-resize.log), [pixel comparison](../build/qa-current-resize-comparison.txt).

[Nine current graphics reloads](../build/qa-current-reload.log) return to the same resource totals in all three cycles: Low 134.46 MiB, Balanced 150.53 MiB, and Ultra 319.39 MiB. Corresponding retained heaps are 384, 384, and 512 MiB. Settled CPU totals are 24.75–24.76 MiB. These runs include shared stateless buffers and transient depth targets. The later spray-lighting change adds no resources.

All sampled fixed-step allocation and packet-overflow counters remain zero. These single-vessel checks do not establish mobile or sixteen-vessel capacity.

## Framework and allocation audit

| Area | Result |
|---|---|
| Forge resources | Buffers, textures, samplers, descriptors, render targets, uploads, command rings, screenshots, and profiling use Forge interfaces. Resources persist between frames. |
| Metal synchronization | Barriers precede encoder completion. The water composite preserves opaque depth. Compatibility changes remain isolated in the Apple patch. |
| Descriptor updates | Water and post passes bind complete sets at initialization and reconnect. This preserves Metal declarations for all referenced render targets. No per-frame update or synchronization was added. |
| FSL | Shared resource tables and shader includes serve the Metal generator. No separate handwritten Metal water renderer exists. |
| Math and algorithms | Graphics transforms use Forge math. Scalar C++17 math and clamp operations remain allocation-free. There is no benefit from replacing them with new wrappers. |
| Timing | CPU timing uses Forge `getUSec`. Only active GPU queries are read. Unused diagnostic fields and a redundant sampler were removed. |
| Flecs | Gameplay uses plain components, body handles, and cached system queries. The bundled adapter supplies Forge allocation, clocks, and profiling. |
| Jolt | Jolt owns vessel motion, collision, compound hulls, and submerged-volume clipping. The application supplies authored mass and inertia. |
| Threads | The current small scene uses Jolt's single-thread job system. Extra scheduling layers would add cost without demonstrated benefit here. |
| Particles | The pinned Forge tree contains `IParticleSystem.h`, but no matching implementation. The current bounded FSL pool specializes in spray, rain, and landing foam. |
| Pools | Commands, propeller-flow history, wave packets, particle slots, and Jolt scratch are bounded. Overflow counters remain visible in the lab. |
| CPU water | FFT storage and calibration scratch are reused. Packet ownership adds one fixed handle per slot. Ordinary force queries allocate nothing. |
| Local reconstruction | A 256² field resolves the attached wake. Compact packet uploads skip inactive GPU records. The new buffers add 1.50 MiB of GPU resources. |
| Sampling | Integer mip levels skip unused samples. Shared camera rays, water queries, and foam material helpers remove duplicate work. All surface rafts now store material coordinates. |
| Spray lighting | Airborne particles reuse the existing phase function and cloud/hull/wave shadows. One camera ray serves depth reconstruction, scattering, and fog. No new buffer, pass, or particle slot was added. |
| Transient depth | Reflection, shadow, and light-entry depth tests use Forge on-tile targets. The main scene depth remains persistent across the opaque and composite passes. |
| Mesh | The water grid shares indexed vertices. Filtered plank joints replace 61 small dock boxes and their aliasing. |
| Optics | Forge supplies GGX distribution and Schlick helpers. Exact correlated visibility remains local because Forge’s joint helper uses a different approximation. Sky radiance uses half precision. |

No application runtime use of `std::vector`, `std::function`, `std::thread`, or a second allocator was found. Small local upload functions remain local instead of becoming a general utility layer.

The audit does not establish a zero-allocation rendering framework. The simulation counter measures calls inside the fixed step. Platform-owned memory and driver allocations remain outside the application counters.

## Remaining work

- Continue comparison of the final fresh-foam material with the reference storm motion and normal overview camera. The twelve-second, 20° rudder sequence retains continuous foam through a turn: [review](../build/mac/Debug/QA/turning-wake-foam/motion-review.png). Shared texture coordinates remove the repeated rotated wake branches in this fixture.
- Move the long waterline review camera clear of the dock before judging its full duration as open-water footage. The present camera has no dock collision handling.
- Improve crest foam thickness, breakup, and spray while retaining the corrected calm response.
- Extend the wake spectrum and twin-hull interference beyond the nine-direction approximation.
- Add automatic background packet injection before claiming complete dock attenuation, reflection, or diffraction.
- Improve rough-water object reflections and cloud shape.
- Validate mobile performance and the later multi-vessel marina budget.
- Calibrate handling and stability against measured vessel data before claiming validated seamanship instruction.

Gameplay expansion remains paused. This review does not approve future crew plans, knots, fenders, mooring constraints, anchors, or scoring.
