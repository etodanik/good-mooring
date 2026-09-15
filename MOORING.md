# Native mooring simulator

The current checkpoint concentrates on water and vessel physics. Gameplay expansion is paused.

The native Mac application contains one vessel, one dock, a seabed, and a continuous perspective camera. Select either Lagoon 42 or Oceanis 40.1 in Session. The existing station prototype restricts direct engine and wheel input to the actor at the helm.

## Build and run

Use an Apple Silicon Mac with the full Xcode application and Metal compiler installed. This build was checked with Xcode 26.5 on macOS 26.6.2. Install CMake and Python 3. The XcodeBuildMCP CLI is optional and only needed for UI launch automation.

From this directory, generate the project:

```sh
cmake -S . -B build/macos -G Xcode -DCMAKE_OSX_ARCHITECTURES=arm64
```

Build and launch the application:

```sh
cmake --build build/macos --config Debug --target MooringSimulator
```

The application is `build/macos/Debug/MooringSimulator.app`. The build compiles FSL shaders and converts the bundled font. The application checks required resources before initialization.

To launch the bundle from a terminal, run `open build/macos/Debug/MooringSimulator.app`.

The [Tracy guide](Docs/TRACY.md) covers CPU and GPU captures, memory, locks, frame images, and profiling build configuration.

Use these controls:

- Drag the water to pan. Scroll or pinch to zoom.
- Select the skipper or a station marker. Select Attend to travel to a station.
- At the helm, use the engine sliders and wheel. W/S controls throttle; A/D controls the wheel.
- Select Session to reset, change vessel, or show diagnostics.
- Press F2 for sea presets, lighting, individual graphics settings, effect views, and the physics lab.
- Select Glass for nearly still water. Select Calm for small waves and long, gentle swell.

The physics checkpoint starts in Advanced, which keeps simulation time running during station interaction. Beginner pauses these interactions. Intermediate uses quarter speed.

## Water

The [technique evaluation](Docs/WATER_TECHNIQUE_EVALUATION.md) compares the implementation with published ocean methods and the current captures. The [QA record](Docs/WATER_QA.md) tracks unresolved visual and performance limits. The current renderer remains below the requested reference quality.

Three directional spectral bands cover 256 m, 64 m, and 16 m. JONSWAP wind sea and independent swell use seeded Gaussian coefficients, directional spreading, and finite-depth dispersion. Uniform current advects the spectrum.

Stockham inverse transforms run through Forge FSL on Metal. Rendering uses persistent buffers for displacement, slopes, and advected compression foam. Both graphics presets use the same physical coefficients and 128² CPU spectrum per band. Their 256² and 512² GPU grids oversample that spectrum. The shortest admitted wavelength is 0.30 m.

A fourth 2 m band adds wind ripples to the shading normals. Its wavelengths span 0.035–0.30 m. Ripple energy approaches zero in light wind. This band contributes no displacement, buoyancy, or foam.

CPU water queries need no GPU readback. They invert horizontal displacement and return surface height, normal, current, and orbital velocity. Velocity attenuation uses each band's representative wave number and finite-depth boundary conditions. This approximation preserves zero vertical flow at the seabed.

Local wakes use eight frequency buckets in a shared 256-packet pool. Busy buckets can borrow unused slots. Nine directions per hull approximate transverse and divergent waves. Hull resistance supplies bounded wake energy. Attached bow, shoulder, and stern pressure profiles also deform the water. Packets propagate, decay, and reflect from the dock's side faces. Local effects fade across an eight-metre boundary region.

Foam follows current, horizontal wave motion, and local eddies. Aging erodes its coverage into smaller patches. Raised particle clumps add stern froth. Droplets and sheets rise above the surface. A shallow optical layer represents aerated water beneath the foam. A bounded crest state ties spray emission to active compression. Orbital velocity controls its launch. Transported age and variable air depth separate fresh breaking from old foam. Bloom adds a restrained glow to bright water highlights.

Wave scattering uses a water-only light-depth capture. Light and view paths determine attenuation through each wave. The lab provides separate controls for this scattering, foam transport, foam geometry, and resource quality. It also exposes 45 diagnostic and beauty views.

The wake source is excluded from its vessel's force queries to avoid counting empirical wave resistance twice. Other vessels and reflected returning waves remain coupled.

Paired packets contribute the interacting field minus the freely propagating field. Identical pairs cancel exactly. Automatic, spectrum-consistent injection of background packets is still absent. Consequently, the FFT background does not yet form a complete dock shadow or diffraction field. This is a wake and reflection prototype, not a full reproduction of the hybrid ocean paper.

## Vessel physics

Jolt owns six-degree-of-freedom vessel motion at 60 Hz. Flecs owns plain gameplay components and physics body handles. Rendering interpolates vessel snapshots.

The selected reference configurations are:

| Property | Oceanis 40.1 | Lagoon 42, 2021 inventory |
|---|---:|---:|
| Hull/reference length | 11.99 m | 12.80 m |
| Beam | 4.18 m | 7.70 m |
| Draft | 2.17 m | 1.25 m |
| Published light mass | 7,985 kg | 12,100 kg |
| Assumed payload | 815 kg | 1,900 kg |
| Simulated mass | 8,800 kg | 14,000 kg |
| Engine arrangement | 45 hp shaft reference | Two 45 hp saildrive references |
| Rudders | Two outboard rudders | Two rudders forward of propellers |

Engine power identifies the reference installation. It does not establish a measured bollard-thrust curve. The current force curves remain estimates.

Each hull uses tetrahedral volume cells. Jolt's polyhedron volume calculator clips each cell against the local water plane. Wet centroids move with heel and trim. Jolt receives buoyancy, pressure-gradient excitation, damping, and resistance at those locations. Authored centres of mass and inertia replace collision-box mass estimates. Graphics, collision, and hydrostatics use the same hull sections and keel dimensions.

Apparent wind acts on separate hull, cabin, and rig areas. Local vessel rotation changes apparent wind and water velocity at each fitting.

Propeller walk and rudder wash are separate forces. Engine response, immersion, advance velocity, shaft orientation, handedness, and ahead/astern coefficients affect thrust. A bounded flow history models transport delay and residual wash. Jet direction and overlap determine which rudders receive that flow.

The Lagoon has no direct ahead jet on its forward rudders. Reverse flow can reach them after transport delay. Its current propeller fit assumes two left-hand propellers; handedness remains independently configurable. A generic central-propeller, single-rudder layout remains available to the physics tests.

Rudder loads use local flow, immersion, cant, finite aspect ratio, bounded lift, and separate reverse efficiency. Unattended steering can back-drive against friction and damping when its brake is released. A braked wheel preserves its setting.

Exact hull offsets, payload distribution, inertia, wind areas, propeller dimensions, thrust curves, foil coefficients, and steering friction still need calibration. The physics lab plots an estimated righting-arm curve and supports release from a selected heel angle. Factory stability curves remain unavailable. Added mass, radiation damping, bank suction, and shallow-water squat remain absent. These limits matter before treating the application as a validated teaching tool.

## Settings and source

Edit `Mooring/Assets/Data/physics.ini`, then rebuild and restart. Settings load at startup. They are not watched while the application runs.

Distances use metres, time uses seconds, and speeds use metres per second. World +Y is up; an unrotated vessel's +X is starboard and +Z is forward. Wind and swell directions specify travel **toward** an angle, measured from +X toward +Z.

Vessel definitions are in `Mooring/Simulation/Hydrodynamics.cpp`. Volume cells, inertia, wind areas, props, and rudders are plain data. Each interactive system remains a small prototype for review.

## Verification

Build and run the regression suite with CTest:

- `MooringTests`: station restrictions, travel, ten-minute simulation, powered handling, bounded commands, and repeated resets.
- `MooringOceanTests`: direct-DFT reference, spectrum energy, deterministic seeds, dispersion, current advection, periodic boundaries, orbital velocity, packets, and a two-minute coupled run.
- `MooringPhysicsTests`: hydrostatic balance, restoring moment, motion decay, coasting, reverse thrust, differential thrust, wash arrival, steering, and grounding.

```sh
cmake --build build/macos --config Debug --target MooringTestSuite
ctest --test-dir build/macos -C Debug --output-on-failure
```

Run an individual binary when its output is useful:

```sh
build/macos/Debug/MooringTests
build/macos/Debug/MooringOceanTests
build/macos/Debug/MooringPhysicsTests build/macos/physics-calibration.csv
```

The calibration CSV records simulated outcomes. It is a regression baseline, not measured sea-trial data.

At startup, the application compares GPU displacement with CPU samples in all three physical bands. It also compares local packet height and slopes, including the patch boundary. Separate checks cover mip averages, ripple energy, foam production, and history isolation. These checks use a temporary ocean and synchronous readback. Normal simulation uses neither readback nor that temporary ocean. Graphics reload checks preserve the live world's packet state.

The [water QA report](Docs/WATER_QA.md) records current captures, timings, memory, and remaining quality gaps. The [rendering notes](Docs/WATER_RENDERING.md) describe the pass structure and reference techniques. These are Mac measurements. Mobile performance remains untested.

The application logs tracked CPU bytes, GPU resource bytes, and GPU heap reservations separately. GPU resources occupy the reported heaps; do not add those two figures. Platform-owned allocations are outside these counters. The initial 512 MiB application budget remains a target for later integrated scenes.

## Port and dependency provenance

The pins are recorded in `Tools/Mooring/dependencies.json`:

- Forge Codeberg: `5abcdd969d0d883e9bf91f6926a8ea0935a0b3bc`.
- Public Apple backend: `cd5046893faba2dc7869243873bf01f02a6f0df9`.
- Flecs: Forge's bundled 4.0.1.
- Jolt: 5.6.0.

`Tools/Mooring/apple-origin.json` records source hashes and identifier translations. Compatibility patches are isolated under `Tools/Mooring/patches`. They cover Metal descriptors, uploads, synchronization, timestamp lifetimes and interval arithmetic, Darwin input, FSL, fonts, and Jolt's bounded broadphase scratch storage.

Jolt's scratch patch uses 256 local body IDs. The physics system has the same 256-body limit. An increase requires another allocation audit.

For a fresh upstream tree, restore Apple files from the pinned reference with `Tools/Mooring/restore_apple.py`. Copy the pinned Jolt source into `Common/Game/ThirdParty/OpenSource/Jolt`. Apply each compatibility patch from the repository root with `patch -p1`. The current workspace already contains these changes. Restoring upstream files again overwrites them.

Mac packaging is implemented through the generated Xcode project. iOS source compatibility guides the port, but an iOS application target and device validation remain outside this checkpoint.

## References

- [Lagoon 42 official specifications](https://www.catamarans-lagoon.com/boats/lagoon-42). Current published dimensions differ slightly from the selected 2021 inventory.
- [Lagoon 42 manufacturer inventory, May 2021](https://www.lagoon-catamaran.de/fileadmin/user_upload/lagoon-42-specifications-may2021-gb-1.pdf).
- [Lagoon 42 manufacturer brochure](https://www.katamarany-lagoon.cz/wp-content/uploads/2020/07/Lagoon42_Brochure.pdf), including propellers aft of the rudders.
- [BENETEAU Oceanis 40.1 specifications](https://www.beneteau.com/en-us/oceanis/oceanis-401) and [shaft-engine configuration](https://configurator.beneteau.com/boat/oceanis-40.1).
- [BENETEAU description of the twin rudders](https://www.beneteau.com/en-us/newsroom-actualite/oceanis-401-and-oceanis-yacht-54-new-wave-oceanis-cruising-yachts-beneteau).
- [YANMAR SD60](https://www.yanmar.com/marine/product/drives/sd60/) and its operation manual. Input-shaft rotation does not establish the installed propeller's handedness.
- [Hybrid ocean paper](https://arxiv.org/html/2511.02852v1). Its desktop results do not establish mobile performance.
- [MMG modular hull–propeller–rudder reference](https://doi.org/10.1007/s00773-014-0293-y). The implementation does not transfer large-ship coefficients to yachts.
- [Oceanology feature reference](https://www.fab.com/listings/87c9af41-62b7-4e70-98e3-fc72eff016ab). No package code or assets were copied.

Crew planning, knots, fenders, line and anchor constraints, neighbouring moored boats, scoring, and the integrated marina remain subsequent checkpoints.
