# Water rendering

The target is stylized Sea of Thieves water, with a separate glassy look in very light wind. Visual parity is not established. [Current captures and open gates](WATER_QA.md) record the evidence.

The [technique evaluation](WATER_TECHNIQUE_EVALUATION.md) compares the current implementation with WaveWorks, research papers, and production water. It distinguishes implemented approximations from the capabilities of those references.

## Reference techniques

| Source | Technique used here |
|---|---|
| [Rare: The Technical Art of Sea of Thieves, 2018](https://history.siggraph.org/wp-content/uploads/2022/09/2018-Talks-Ang_The-Technical-Art-of-Sea-of-Thieves.pdf) | FFT waves, persistent foam, a foam texture, depth intersections, crest translucency, and an area sun highlight. |
| [Rare: Inn-side Story #16 — Storms](https://www.seaofthieves.com/news/inn-side-story16) | Reference for coordinated wind, rain, waves, and vessel response. |
| [GPU Gems, chapter 1](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-1-effective-water-simulation-physical-models) | Separate geometry and normal detail. Filter displacement against mesh spacing. |
| [Tessendorf: Simulating Ocean Water](https://evasion.inrialpes.fr/Membres/Fabrice.Neyret/NaturalScenes/fluids/water/waves/fluids-nuages/waves/Jonathan/articlesCG/simulating-ocean-water-01.pdf) | Spectral displacement, Fresnel reflection, transmission, and water absorption. |
| [Bruneton, Neyret, and Holzschuch, 2010](https://evasion.inrialpes.fr/Publications/2010/BNH10/article.pdf) | Transfer scalar unresolved slope variance into the sun highlight and filtered sky reflection. Directional covariance and rough object reflections remain absent. |
| [Hybrid ocean paper, 2025](https://arxiv.org/html/2511.02852v1) | Reconstruct shared wave packets on CPU and GPU. Subtract the free field from paired interacting packets. |
| [Tessendorf, Reinhardt, and Gao: Whitecap Fraction, 2020](https://jtessen.people.clemson.edu/gilligan/html/whitecap_fraction.pdf) | Calibrate a breaking threshold from the distribution of minimum surface stretch. |
| [Guerrilla: volumetric cloudscapes](https://www.guerrilla-games.com/read/the-real-time-volumetric-cloudscapes-of-horizon-zero-dawn) | Cached periodic Perlin/Worley shape, height profiles, detail erosion, volume integration, and approximate directional scattering. |
| [GPU Gems, chapter 16](https://developer.nvidia.com/gpugems/gpugems/part-iii-materials/chapter-16-real-time-approximations-subsurface-scattering) | Estimate the light path through a volume from a light-view depth capture. |
| [Valve: Water Flow, 2010](https://cdn.steamstatic.com/apps/valve/2010/siggraph2010_vlachos_waterflow.pdf) | Blend overlapping texture-advection phases to limit stretching without visible resets. |
| [McGuire and Mara, 2014](https://jcgt.org/published/0003/04/04/paper.pdf) | Perspective-correct screen-space intervals for reflection tracing. |

The ACM endpoint rejected access. The SIGGRAPH archive supplies the same two-page Rare paper. It does not supply the complete production renderer. No Rare or Guerrilla assets or code are included.

The reference screenshots show broad wave shapes, bright translucent crests, broken foam trails, and fine highlights within larger smooth regions. Extra high-frequency contrast alone does not reproduce that look. The calm photographs also show coherent sky reflections without the same short-wave energy as windy water.

The storm comparison exposed three failures: foam concealed the troughs, large waves lacked contrast, and airborne spray was absent from most visible crests. Rare's paper describes sea-state-dependent foam generation, dispersion, and blending. The revised foam lifetime follows that principle. The storm preset combines a shorter wind sea with a longer swell. It does not amplify geometry separately from physics.

## Pass structure

1. Three physical FFT bands cover 256 m, 64 m, and 16 m. A fourth 2 m band supplies only fine shading normals.
2. Stockham transforms reconstruct displacement, slopes, compression, and persistent spectral foam. Each group transforms one complete row or column through shared memory.
3. A fixed packet pool and attached hull disturbances reconstruct local displacement and slopes on a 256² grid.
4. Mip chains retain mean slopes and a scalar second moment. Unresolved variance broadens the sun highlight and sky reflection.
5. Spectral and local fields transport foam age, entrained air, and air depth. Compression and orbital motion drive persistent crest sources.
6. A persistent RGBA16F texture stores sky radiance, cloud opacity, and filtered levels. A 64³ RG8 texture supplies cached cloud shape and erosion.
7. Separate passes render opaque color with view depth, an object reflection capture, and a shadow map. A water-only light-depth pass records light entry through the displaced surface.
8. One composite pass draws the sky, water, and particles against the opaque depth buffer.
9. Atmospheric rays integrate into a quarter-resolution buffer. Depth-aware filtering preserves foreground silhouettes.
10. Three reduced-resolution HDR levels spread bright highlights. Bloom enters before tone mapping and FXAA. UI renders last.

The Metal backend requires resource barriers before an encoder ends. The composite pass retains its depth attachment. Splitting these operations incorrectly allowed earlier scene writes to overwrite water tiles.

All resources persist during normal rendering. Frame constants use mapped Forge buffers. Resize or an explicit graphics apply waits for the queue before replacing resources.

Normal moments and local displacement share one buffer each across frames. Neither field needs history. Ordered queue work and existing barriers protect their reuse. This removes 22.33 MiB of Ultra resource storage.

Reflection, object-shadow, and water-light depth tests use Forge on-tile targets on Apple silicon. Their depth is needed only within one render pass. The main scene depth stays persistent because the composite loads it. This saves 77.56 MiB of Ultra resource storage; all 200 comparison captures remain pixel-identical. Startup still reserves 608 MiB of heaps. After resize or quality reload, Ultra reserves 512 MiB.

Scene and water descriptor updates supply each complete resource set. This Forge revision rebuilds Metal residency declarations on every update. Partial updates dropped declarations for previously bound render targets and caused inconsistent diagnostic captures.

## Surface detail and calm water

Both GPU resolutions use the same 128² physical coefficients in each band. The 256² and 512² transforms oversample those coefficients. A graphics change cannot increase physical wave energy.

The normal-only band spans wavelengths of 0.035–0.30 m. Its RMS slope approaches zero below 0.75 m/s wind. Glass retains small swell without the dense ripple pattern. Calm adds a small wind sea. The visual detail multiplier increases between 5 and 14 m/s wind. Increasing it cannot change vessel motion.

The physical spectrum also has a smooth short-wave rolloff, `exp(-k² × 0.12²)`, before its 0.30 m cutoff. This follows [Tessendorf's equation 41](https://people.computing.clemson.edu/~jtessen/reports/papers_files/coursenotes2004.pdf). The 0.12 m coefficient is visual tuning. Each source retains its specified significant height after normalization. CPU and GPU use the same revised coefficients. The separate fine band retains wind ripples.

The indexed grid shares vertices between adjacent triangles. Its dense region covers the viewer and camera focus, then expands continuously toward the horizon. Geometry uses mesh-spacing filtering. Shading uses pixel-footprint filtering.

The sun has a finite angular radius of 0.00465 radians. The water BRDF includes Fresnel and correlated masking for view and light directions. Normal filtering interpolates each texel's unresolved variance separately from its resolved slope. Screen derivatives supply within-pixel slope variation. Treating interpolation between resolved slopes as roughness previously produced broad, muddy highlights.

Mean environment Fresnel follows Bruneton's fitted rough-surface expression. The scalar variance assumes equal variance along both tangent axes. Forge's `D_GGX` and `F_Schlick` helpers serve the direct sun. The exact correlated visibility term remains local because Forge's available joint term uses a different approximation.

The residual roughness control defaults to 0.05. Its wind multiplier falls to 0.16 in glass conditions. This softens resolved highlights without changing the wave spectrum. Broad silvery reflection bands remain a visual limitation.

Sky reflection selects filtered levels from the same scalar slope variance as the sun highlight. Spherical-area weights preserve mean radiance through the mip chain. Visible sky and distance haze use positive cubic reconstruction to reduce magnified cache cells. Object reflection still traces one direction. Directional covariance and a rough object-reflection filter remain absent.

The sky now uses a Forge RGBA16F texture. Paired positive cubic weights need four hardware bilinear reads instead of sixteen manual texel reads. The filter follows [Sigg and Hadwiger](https://developer.nvidia.com/gpugems/gpugems2/part-iii-high-quality-rendering/chapter-20-fast-third-order-texture-filtering). Longitude wraps and latitude clamps at the poles. Explicit mip views preserve the spherical-area reduction and its energy checks. Texture filtering introduces small rounding differences, documented in the QA record.

## Foam, wakes, and spray

Spectral foam has advection, diffusion, and decay. History uses undisplaced wave coordinates; mesh displacement supplies orbital motion, while current transports history. Adding orbital transport again moved foam twice. Positive breaking crests recharge the field continuously. The onset interval remains below unit stretch, including after a threshold edit, so flat water cannot break. Foam decays faster after a crest passes; short-wave foam decays faster again.

Active spectral crests use a production rate of 18 per second. Decay increases fivefold when breaking stops. Local wake sources retain their rate of 6 per second and longer persistence. These are bounded visual lifecycle coefficients, not measured bubble kinetics.

Shading approximates surface area from filtered band stretch and combined slope. Compression gathers spectral foam. Expansion and steep faces dilute it. The correction is bounded from 0.25 to 4 and scales the transported moments together. The product of band stretches neglects cross-band shear. Local foam already includes flow divergence and does not receive this correction twice.

The former area ratio used screen derivatives of rasterized triangles. It imprinted mesh facets into foam density as the camera or window changed. The filtered approximation adds no buffer.

Foam age is transported as concentration multiplied by age. Active breaking remains a separate channel, so old foam cannot restart crest spray. Active breaking and retained air select dense, porous churn. As air dissolves and foam ages, that material opens into lace and eroded remnants. Unresolved texture coverage blends toward average density. Nonzero density and particle counts do not establish convincing foam in beauty.

Two overlapping texture phases follow local flow. A phase resets only when its blend weight is zero. Smooth eddies and wave slopes distort larger breakup patterns. A capped normal perturbation adds shading relief without changing the water mesh. The lab separates this relief from the height of particle foam.

Water and raised wake foam now share the same material coordinates, advection phases, texture octaves, and lighting helper. Particle UVs shape each parcel's boundary only. They cannot rotate the foam texture. Older wake parcels develop pores and lose their raised height exponentially. Active crest froth retains height while its source replenishes it. Fresh foam combines two independently filtered cluster scales. Its optical coverage retains internal variation at high density. Older foam exposes the larger lace holes.

Configuration computes a histogram of minimum surface stretch. Combined long and middle bands share one threshold. The short band has another. Each graphics resolution uses its actual derivative spacing. The threshold follows a wind-based whitecap fraction, with a provisional 85/15 split between those groups. This split and the wind-only relationship remain tuning approximations. Histograms reuse ocean scratch storage and require no work during ordinary simulation steps.

The coverage calibration uses a 0.70 feedback factor after the faster remnant decay. This restores active crest coverage. It does not make the wind relation an exact prediction of visible foam area.

The local 64 m field retains wake and dock foam. Sources use the vessel's actual hull spacing and propeller positions. The Lagoon therefore produces separate hull trails. Existing current and previous FFT buffers supply horizontal surface velocity. No additional history buffer or GPU readback is needed. A bounded divergence correction gathers concentration in converging flow and thins it in expanding flow. Curl noise supplies smooth eddies; it also varies decay. This remains an approximate transport model.

Stronger stern injection supplies a continuous transported trail. Reduced raft emission limits overlapping foam geometry. These adjustments redistribute visible wake foam; they do not change vessel forces or wake displacement.

Paused updates preserve density and velocity. Repeated redraws do not advance decay, particles, or texture phases. The global current remains valid outside the moving local field.

Attached hull disturbances add bow pile-up, shoulder drawdown, and stern recovery. Dynamic head determines their amplitude; their longitudinal profile balances positive and negative volume. Each hull has its own source. Reverse motion reverses that profile. The CPU and GPU use the same coefficients, with analytic gradients checked against finite differences. This is an empirical moving-pressure approximation, not a CFD solution. Its tunable coefficients reside in `VesselLayout`.

The particle pool has a fixed maximum of 4,096 entries. Separate slot ranges serve hull spray, surface foam, wave crests, and rain. Droplets and spray sheets follow ballistic motion. Short-lived mist follows wind and drag. Surface foam follows local flow and the displaced water surface. Eight triangles form each raised foam clump. It settles with age, receives directional lighting, and erodes through its texture. This is froth geometry, not a volumetric fluid solver. Brief landing foam releases airborne slots quickly. Forge's gradient texture sampling selects particle texture mips without a hard-coded texture size.

Rain now expires at the water surface without creating a foam raft. Raised crest froth retains its tracked source strength in an existing particle channel. When that source passes, relief and opacity settle with a 0.1-second response. The underlying transported foam remains. This change adds no resource or particle slot.

A pair of 4 KiB buffers holds 256 crest sources in fixed 4 m world cells. The 64 m region covers the viewer and vessel. A five-by-five search starts an event; a smaller search tracks its compression maximum for up to 2.5 seconds. Compression controls emission. Vertical and horizontal orbital motion control launch, without a second velocity threshold that rejects turning crests. Low, Balanced, and Ultra assign one, four, and eight particle slots per source. Culling projects the displaced crest height. Paused redraws preserve source position, strength, and age, including after an aspect-ratio change.

Balanced and Ultra reserve one existing slot per source for replenished crest froth. It follows the tracked lip while breaking continues, then drifts and ages as a material remnant. Other froth parcels retain their material coordinate from birth. Both map that coordinate forward onto the surface, avoiding an unnecessary inverse lookup near compressed crests.

New crest parcels use the least-compressed direction of the local displacement field. It runs along the breaking lip, including where swell crosses the wind sea. Unlike a tangent from height gradients, it remains defined at a flat crest summit. Nearly isotropic strain falls back to the wind's transverse direction. Each parcel retains its birth orientation and eight-triangle geometry budget.

Measured vertical and horizontal wave velocity scale each froth parcel's size. Small breakers therefore shed smaller clumps than storm crests. This uses the existing launch calculation and adds no storage.

The sampled wave foam mass also controls each crown's height and optical thickness. Depleted regions lose raised coverage. Surface foam and crowns share textured thickness, with connected structures and procedural pores. Fresh foam therefore retains water transmission instead of bypassing its larger texture breakup.

Airborne groups contain four compact parcels with filtered procedural droplet coverage. A warped fine-grain field removes the former visible rows of beads. The parcels share one ballistic state; they are not independent simulated droplets. Porous, raised crowns follow the actual wave at each vertex. Their coverage thins toward the mesh boundary instead of exposing a sharp disc. This removes the former stretched lace webs.

Airborne droplets and mist combine ambient light with directional scattering. They reuse the water phase function and cloud, hull, and wave shadows. Backlighting can brighten exposed spray while occluded particles retain ambient light. Mist remains small and faint. Hull emitters use separate positions and speed-based strength. These are bounded effects driven by the wave field, not resolved overturning or a continuous breaking-front solver.

Wake packets use finite-depth dispersion and a stationary-wave relation for their wavelength. Nine directions per hull approximate transverse, cusp, and divergent waves. Both catamaran hulls emit inward and outward branches, allowing interference in the tunnel. The total emission remains within one resistance-energy budget. Their lower group speed leaves a trailing wedge. A wavelength-relative steepness limit replaces the former fixed 18 cm amplitude cap. Smooth edge weights keep height and slope continuous at the local patch boundary, including corners.

Packets and attached disturbances record their source vessel. A vessel excludes its own outgoing wake from force queries because empirical hull resistance already includes that energy loss. Other vessels still receive the wake. Reflection clears the source exclusion, so returning waves can affect the original vessel.

Nine directions still approximate a continuous wake spectrum. Automatic injection of background-spectrum packets is absent. The dock cannot yet cast a complete wave shadow or diffraction field.

A black Calm FFT-foam view is expected. Whitecaps and Storm produce nonzero foam. Startup checks reject missing storm foam and excessive coverage. The raw density view applies no hidden gain. The inspection control amplifies weak signals explicitly. View 26 lights the rasterized mesh directly, without normal maps or foam, to expose actual geometry.

## Light and optical limits

Wave scattering integrates light along the refracted view ray. A water-only light-depth capture supplies the distance from the illuminated surface to each interior sample. RGB attenuation depends on that distance and the path toward the viewer. The calculation adds the change relative to a flat half-space; the deep-water tint already represents its baseline. This avoids adding the same cyan contribution throughout the sea.

Wave height and compression no longer paint an independent bright colour mask. Changing the sun while holding waves fixed changes which faces transmit light. Samples, contrast, and light-depth resolution have separate lab controls. View 10 isolates positive scattering; view 32 shows mean light path, with white representing 12 m.

The same light-entry capture shadows direct sun glints and foam behind neighboring waves. Raised foam uses the same hull and wave shadow helpers as the water surface. A biased, softened depth comparison limits self-shadow artifacts. Volume transport already includes this water path and does not receive that surface shadow twice.

This remains a single-scattering approximation with artistic coefficients. The light-entry capture spans 256 m around the camera focus. Outside it, the calculation blends to a flat-surface fallback. It does not refract sunlight at its entry point or solve multiple scattering.

A diffuse-sky approximation shares the measured light path under overcast conditions. It does not integrate the full sky hemisphere. The phase function remains an artistic approximation of water scattering.

Each whitewater sample stores half-precision foam-age mass, air concentration, air-depth mass, and active breaking. The simulation transports these moments with foam. Breaking injects air at depths from 0.18 to 1.5 m, according to local motion. Bubbles rise at 0.22 m/s and dissolve. Filtering moments before division preserves age and depth at patch edges. Only bands 0 and 2 require whitewater history. Spectral and local storage costs 3.67 MiB in Balanced or 11.67 MiB in Ultra.

The refracted view path through this variable layer controls under-foam scattering. View 33 isolates its contribution; views 34–36 expose active breaking, foam age, and air/depth. This is a depth-column approximation, not three-dimensional bubble transport.

The Storm preset uses 26 m/s wind, 5 m wind-sea height, 30 km fetch, and 2.5 m swell with a 10 s period. Heights are significant wave heights. Choppiness is 1. These are prototype scenario values. Heavy overcast suppresses direct sun glints and shifts the reflected light toward a darker storm palette.

Whitecaps uses 19 m/s wind, 2.8 m wind-sea height, 16 km fetch, and 1.2 m swell with a 7 s period. Shorter fetch produces a younger wind sea with shorter dominant waves. Preset changes affect the shared physical sea. Lighting and foam appearance controls remain visual only.

Refraction marches a refracted ray through opaque view depth, then refines a surface crossing. It rejects each foreground texel before filtering. This prevents bright hull and rail outlines from leaking into the water. Absorption depends on the underwater path length.

The current seabed is flat. Its analytic intersection supplies a continuous fallback when a screen trace misses. Submerged objects above that floor use traced color. This fallback needs replacement when the marina gains varied terrain.

Reflection rays start on the displaced wave and follow its normal through the mirrored object-depth capture. Perspective-correct intervals locate depth crossings. Filtering accepts only nearby depths, avoiding expanded rail silhouettes. Conservative projected object bounds reject empty rays before texture access. The single capture still misses occluded and off-screen geometry.

Downward environment rays approximate one reflection from the surrounding mean sea. Fresnel blends that reflection with the water body continuously at grazing angles. This replaces the dark horizon transition; it does not trace neighbouring waves or solve multiple reflections.

The shadow map uses texel snapping and filtered depth comparisons. Its projection preserves depth along the light direction when the vessel moves. It supports 1,024², 2,048², and 4,096² resolutions within a 64 m region.

Clouds use a small cached volume texture and bounded ray samples. A broad weather field varies coverage and warps the repeating volume. Water, scene lighting, and atmospheric rays sample the same cloud density. The height-dependent threshold retains the broad shape before filtering fine erosion. Sky radiance uses half-precision storage and full-precision interpolation.

The latest bake separates larger Perlin/Worley billows from fine erosion. Two rotated shape samples have more balanced weights, so broad groups remain visible. The texture size, ray steps, and sample count stay unchanged. Repeated silhouettes and soft edges still limit the reference match.

Heavy overcast adds a lower cloud layer and neutral ambient lighting beneath the towers. It shares the existing noise samples and adds no storage.

The directional cache updates one interleaved quarter per frame in Low and Balanced, or one eighth in Ultra. Angular resolution stays unchanged. Weather changes and large camera jumps fill every direction immediately. Paused redraws preserve the cache. Temporal weights account for the update interval. The filtered sky levels add 1.33 MiB in Balanced or 5.33 MiB in Ultra. Cloud shapes remain a visual quality limit.

Underwater rays sample hull shadows along the refracted path and reuse the entry-point cloud shadow over their maximum 12 m length. Cloud features span hundreds of meters. Atmospheric rays integrate below the cloud layer. Both remain approximate single-scattering effects. Breaking surf, caustics, underwater gameplay, and a complete planetary atmosphere are outside this checkpoint.

Bloom extracts highlights after exposure in linear HDR. A normalized 13-tap reduction builds quarter-, eighth-, and sixteenth-resolution levels. Their weights are 0.6, 0.3, and 0.1. The default strength is 0.08; Low disables the passes. View 37 isolates bloom, including calm-water sun glints. The three targets cost about 1.44 MiB at 1920 × 1200. Resize recreates them from drawable dimensions.
