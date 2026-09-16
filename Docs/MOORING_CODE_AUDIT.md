# Mooring code audit

Date: 2026-09-16. Baseline: `0f39677`.

Scope: application setup, rendering, simulation, water, shaders, tool windows, and existing tests in `Mooring/`.
The requested search-field fixes also cover Forge's UI wrapper and macOS input path.

## Findings and changes

| Finding | Change |
| --- | --- |
| Shader Lab copied owned strings when it recorded pipeline bindings. | Binding records now reference the static names in Forge's FSL resource tables. Resource handles and binding offsets remain snapshots. |
| The browser allocated temporary search strings, result lists, labels, and wrapped lines every frame. | It now reuses storage, skips search text construction for empty queries, and uses string views for label slices. |
| Shader reload constructed temporary renderers with large, unused arrays. | Reload now stages plain shader and pipeline handle structures. It swaps them into place only after successful creation. |
| Sea Lab and Shader Lab had separate case-insensitive searches and duplicated UI helpers. | Both use Forge's `binstrcaseless` through `ToolUI`. Shader Lab also uses the shared button, heading, label, and number controls. |
| Short names obscured simulation, FFT, rendering, and test code. | Local variables and parameters now describe their roles. Examples include `waveNumber`, `propellerIndex`, `frameIndex`, and `pipelineDescription`. |
| Repeated literals hid physical units and buffer capacities. | Gravity, water density, air density, propeller history capacity, command capacity, and deck node counts now have names. |
| Some code stored or forwarded information without using it. | Removed unused capacity constants, the unused ECS vessel-layout tag, and the footer-height forwarding helper. |
| Helm access repeated the same ECS lookup. | It now fetches the actor once, after the bounds check. |
| Ocean interpolation copied each surface sample before reading it. | It now reads through a const reference. |
| Search fields clipped their text and used a multiline editor. | Both labs now use Nuklear's single-line editor. Row height includes the font, editor padding, and border. |
| macOS never populated Forge's repeat events for editing keys. | AppKit now forwards editing keys and OS repeats. Key presses and their modifiers survive until the frame consumes them. |

Reload staging no longer initializes the temporary renderer's 1 MiB spectrum array or the temporary scene's 320 KiB mesh array.
This reduces reload memory work; it is not a measured frame-rate improvement.

Changed C++ files use the repository's clang-format rules.
Standard coordinate components such as `Vec3::x` and quaternion `w` retain their conventional names.

## Forge integration

The application already uses Forge's resource loader, FSL resource tables, descriptor sets, command rings, fences, and resource barriers.
Those APIs remain the rendering foundation.
The audit also checked descriptor-name lifetimes against the bundled FSL macros and Metal implementation.

Complete descriptor updates remain intact because Metal rebuilds residency information when an argument buffer changes.
Render passes, resource formats, dispatch counts, and draw order are unchanged.
Shader source and floating-point calculation order are unchanged.
The search-field height and editing fixes are the user-requested exceptions to preserving UI behavior and appearance.

## Structures retained deliberately

- Fixed simulation arrays and reusable FFT scratch prevent allocations during steady-state physics steps.
- Forge/Jolt conversion helpers keep ECS and GPU data layouts independent of engine vector types.
- Small vector and complex-number values remain value types.
- Shader Lab keeps the completed GPU readback copy. It preserves displayed samples when a frame buffer is reused.
- Resource creation and upload helpers retain their resource tracking and synchronization responsibilities.
- Debug registry metadata keeps owned strings where reload can replace the original source data.

Further GPU optimization needs separate measurements and visual review, especially for water sampling, reflections, and cloud noise.
This audit does not claim a reduction in shader execution time.

## Validation

- Debug and RelWithDebInfo application builds passed.
- All four existing suites passed: Simulation, Ocean, Physics, and Tracy.
- Existing steady-state simulation allocation checks passed.
- Compiler checks found no unused functions, unused variables, shadowing, range-loop copies, redundant moves, or unreachable code.
- Metal validation and the startup GPU/CPU water comparisons passed. Forge reported no memory leaks on shutdown.
- All 24 deterministic Ultra Storm captures were pixel-identical to the baseline.
- Shader Lab QA passed: seven function previews, twelve resource cases, and reload with resource preservation.
- Manual checks passed for search height, Backspace, forward Delete, cursor navigation, Shift selection, select-all, replacement, and mixed-case filtering.
- Pipeline names, binding labels, and navigation from a pipeline to its resource preview passed manual checks.

Build and regression commands:

```sh
cmake --build cmake-build-debug --target MooringSimulator MooringTestSuite -j8
cmake --build build/profile-validation --target MooringSimulator -j8
```

Graphics checks:

```sh
MTL_DEBUG_LAYER=1 cmake-build-debug/MooringSimulator.app/Contents/MacOS/MooringSimulator --water-qa-ultra --water-qa-scene=2
MTL_DEBUG_LAYER=1 cmake-build-debug/MooringSimulator.app/Contents/MacOS/MooringSimulator --shader-lab-qa
```

Validation covers the current macOS/Metal build. Other graphics backends were not executed.
