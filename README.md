# Mooring Simulator

Mooring Simulator is a native macOS/Metal game prototype for experimenting with
boat motion, hydrodynamics, hydrostatics, and water rendering. The repository
contains the game and only the Forge runtime pieces required to run it.

## Repository layout

- `Mooring/` — game source, shaders, assets, simulation code, and tests.
- `Common/` — the shared Forge runtime and the third-party libraries used by the game.
- `Tools/Mooring/` — resource generation, Tracy integration, and QA tools.
- `Docs/` — focused design and verification notes.
- `MOORING.md` — the detailed project guide.

[Tool windows](Docs/TOOL_WINDOWS.md) covers the command bar, clean view, and frame counters.
[Shader Lab](Docs/SHADER_LAB.md) covers shader reload, function previews, and GPU resource inspection in the Debug app.

The old Forge example tree and sample/unit-test applications are not part of
the project. Visibility Buffer examples are not used by Mooring and have also
been removed.

## Requirements

- macOS 15 or later on Apple silicon
- Xcode with the Metal SDK
- CMake 3.25 or later
- Python 3

The resource build uses Forge's shader tools. The image-based QA checker also
uses Pillow when capture inspection is required.

## Configure and build

```sh
cmake -S . -B build/macos -G Xcode -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build/macos --config Debug --target MooringSimulator
```

The application bundle is produced by the `MooringSimulator` target. Generated
resources are kept in the build directory and are not written into the source
tree.

## Tests and QA targets

Run the registered tests with:

```sh
ctest --test-dir build/macos -C Debug --output-on-failure
```

The CMake project also provides these targets:

| Target | Purpose |
| --- | --- |
| `MooringResources` | Build shaders and the game font. |
| `MooringTestSuite` | Build and run the CTest regression suite. |
| `MooringPhysicsCalibration` | Write physics calibration data to `test-results/`. |
| `MooringRun` | Run the game with optional `-DMOORING_RUN_ARGS=...`. |
| `MooringQA` | Run the balanced water capture and inspect it. |
| `MooringQAUltra` | Run the high-quality water capture and inspect it. |
| `MooringQAMotion` | Capture a moving water scene and inspect it. |
| `MooringQAResize` | Exercise the window resize path. |
| `MooringQAReload` | Exercise shader/resource reload. |
| `MooringClean` | Remove generated resources and QA captures. |

For example:

```sh
cmake --build build/macos --config Debug --target MooringTestSuite
cmake --build build/macos --config Debug --target MooringQAUltra
```

QA captures are written below the application build directory in `QA/`.
They are temporary verification artifacts and should not be committed.

## Performance profiling

Configure a separate build with Tracy instrumentation:

```sh
cmake -S . -B build/profile -G Xcode \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DMOORING_PROFILE=ON \
  -DMOORING_PROFILE_FRAMES=1800
cmake --build build/profile --config RelWithDebInfo --target MooringProfile
```

`MooringProfile` waits for a Tracy server, runs an unthrottled water benchmark,
and exits after the requested number of frames. `MooringProfile` is intentionally
unavailable in a build configured without `MOORING_PROFILE=ON`.

## Further reading

See [MOORING.md](MOORING.md) for controls, physics notes, troubleshooting, and
current limitations. The [water QA guide](Docs/WATER_QA.md) and
[Tracy guide](Docs/TRACY.md) describe the focused verification workflows.

The shared runtime is derived from [The Forge](https://github.com/ConfettiFX/The-Forge)
and remains available under the repository's existing license.
