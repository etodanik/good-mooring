# Tracy profiling

The application supports [Tracy 0.14.1](https://github.com/wolfpld/tracy/releases/tag/v0.14.1).
The build pins the source archive and its SHA-256 checksum.
Normal builds exclude Tracy. Profiling builds use on-demand recording by default.

## Build

From the repository root, generate a separate profiling project. `MOORING_PROFILE`
enables Tracy and exposes the `MooringProfile` run target:

```sh
cmake -S . -B build/profile -G Xcode \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DMOORING_PROFILE=ON

cmake --build build/profile --config Debug --target MooringSimulator
```

For optimized measurements, use `--configuration Release`. Symbols and frame pointers remain available.
Debug includes the Forge allocation tracker. Release uses the normal allocator with serialized Tracy allocation events.

| CMake configuration | Default | Effect |
|---|---|---|
| `MOORING_TRACY` | `OFF` | CPU, GPU, memory, locks, plots, messages and frame support |
| `MOORING_TRACY_FINE` | `OFF` | Individual water samples, wave packets, hull clipping and hydrodynamic queries |
| `MOORING_TRACY_STACK_DEPTH` | `8` | Allocation and free callstack depth, from 0 to 62 |
| `MOORING_TRACY_ZONE_STACK_DEPTH` | `0` | CPU zone callstack depth, from 0 to 62 |
| `TRACY_ON_DEMAND` | `ON` | Record only during a connection |
| `TRACY_ONLY_LOCALHOST` | `ON` | Accept connections from the local machine |
| `TRACY_NO_BROADCAST` | `ON` | Disable automatic network discovery |

Fine zones and CPU callstacks substantially increase recording cost. Use them for short investigations after a coarse capture identifies a problem.
An allocation depth of zero retains allocation events without callstacks.
`TRACY_ON_DEMAND=OFF` retains startup events before the first connection and permits one capture per application run.

## Record

Use the Tracy 0.14.1 profiler GUI. Connect to `127.0.0.1:8086`.
The version must match the application client.

Launch the application, or use `MooringProfile` for the repeatable benchmark:

```sh
open "$PWD/build/profile/Debug/MooringSimulator.app" --args \
  --tracy-wait --tracy-images=15
```

For the benchmark target:

```sh
cmake --build build/profile --config Debug --target MooringProfile
```

`--tracy-wait` waits up to 30 seconds before application initialization.
`--tracy-images=15` records a thumbnail every fifteenth render frame. Zero disables thumbnails, which is the default.
The profiler's **Thumbnail interval** parameter controls the same feature during a capture.
Thumbnails use a GPU downsample and delayed readback. The CPU does not wait for the thumbnail.
The image pass joins the renderer's GPU fence chain, so it cannot read an unfinished frame.

For a repeatable storm run, add these application arguments:

```text
--water-qa-ultra --water-qa-scene=2 --water-benchmark=1800
--water-profile --benchmark-unthrottled
```

`--water-qa` selects Balanced quality. Scene 0 selects calm water, 3 selects the wake, and 8 selects whitecaps.
`--water-qa-resize` exercises five window sizes. `--water-qa-reload` exercises graphics resource replacement.
The [water QA guide](WATER_QA.md) describes the remaining scenarios and diagnostic views.

## Capture without the GUI

Build the official capture tool and the local trace inspector:

```sh
cmake -S Tools/Mooring/TracyTools -B build/tracy-tools \
  -DCMAKE_BUILD_TYPE=Release \
  -DTRACY_SOURCE_DIR="$PWD/build/profile/_deps/tracy-src"
cmake --build build/tracy-tools --target tracy-capture mooring-tracy-inspect -j8
```

If CMake uses an existing Tracy source checkout, set `TRACY_SOURCE_DIR` to that checkout instead.
The tools require a C++20 compiler. Their dependencies come from the pinned Tracy source.

Start the capture before the application:

```sh
build/tracy-tools/capture/tracy-capture -a 127.0.0.1 -o build/storm.tracy
```

The capture ends with the application. `-s 15` stops recording after 15 seconds.
With on-demand recording, another capture can connect to the same application.

Inspect the saved capture:

```sh
build/tracy-tools/mooring-tracy-inspect build/storm.tracy \
  build/storm-thumbnail.dds --app --images
```

The inspector checks CPU and GPU coverage, timing validity, memory pools, locks, plots, sections and optional images.
It prints inclusive zone totals. Parent and child totals overlap.
The optional DDS output contains an actual thumbnail from the capture.
The `.tracy` file remains compatible with the official GUI.
Add `--messages` to print recorded logs, including rejected GPU timestamp details.
Add `--gpu-stages` to require render-stage timings and validate their parent intervals and ordering.

To investigate transient corruption, record with `--tracy-images=1` and export every thumbnail:

```sh
build/tracy-tools/mooring-tracy-inspect build/storm.tracy \
  --app --images --images-dir=build/storm-frames
```

These images include intermediate frames between QA screenshots. Screenshot readback pauses the application and can hide synchronization faults.
For a complete `--water-qa-ultra` or `--water-qa` recording, check the paused sequences with Pillow:

```sh
python3 Tools/Mooring/check_tracy_frames.py build/storm-frames \
  --report build/stable-frames.json
```

This check compares all three frames of each paused diagnostic. It excludes the animated intervals.

## Coverage

| Area | Recorded information |
|---|---|
| Application | Initialization, shutdown, update, drawing, input, camera, interaction, UI, scene replacement, resize and resource reload |
| Simulation | Commands, crew travel, vessel forces, fixed steps, snapshot work, Jolt external profiling and named Flecs systems |
| Ocean | Spectrum configuration, CPU FFTs, uploads, GPU FFTs, local waves, whitewater, particles, sky, geometry, scattering and composite passes |
| Graphics | Renderer and resource lifetime, descriptors, pipeline creation, command encoding, draw/dispatch sizes, barriers, transfers, submission and presentation |
| GPU timeline | Graphics, upload and copy queues, encoder intervals, vertex and fragment stages, and original CPU encoding timestamps |
| Threads | Forge thread names, mutex ownership and contention, recursion, try-locks, condition waits, joins, sleeps and task boundaries |
| Files | Open, mapping, read/write byte counts, seek, flush and close |
| Frames | Render frame boundaries and independent 60 Hz physics intervals |
| Context | Scenario sections, source locations, zone text and values, application version, logs and severity |
| Plots | Frame intervals, window dimensions, diagnostics, memory, speed, packet counts, simulation errors and allocation counts |
| Images | Optional frame thumbnails and a live interval parameter |

The Forge CPU profiler bridge retains existing named scopes, including font and UI work.
The native GPU bridge uses encoder boundaries. It does not split encoders or insert barriers to time individual draws or dispatches.
CPU zones identify individual encoding calls. Encoder names identify passes or the pipelines within each encoder.

Each render encoder includes `Vertex` and `Fragment` child zones when its stage timestamps are valid.
For example, `Surface / HDR copy + Ocean surface + Foam and spray` retains its total duration and includes both children.
These children measure all vertex work and all fragment work within that encoder.
They do not assign separate GPU durations to its individual draws.
Their CPU intervals describe the same encoder submission work.

The encoder interval runs from the vertex start to the fragment end, including any gap between stages.
Parent and child totals overlap, so their sum is not a frame duration.
Invalid stage timestamps omit the children but retain a valid encoder interval.
Compute and transfer encoders retain their existing timers.

The Surface encoder includes the HDR copy, ocean surface, and foam and spray.
Bloom runs in three existing post-processing encoders, named `Post / Bloom level 0` through `Post / Bloom level 2`.
The atmospheric pass uses `Post / Atmospheric integration`.
These labels distinguish the operating modes of the shared post-processing pipeline.

The Metal bridge resolves completed command buffers through their existing lifecycle.
It retains sample buffers until collection and discards results from an earlier connection.
Each queue publishes completed intervals in submission order, including work encoded on different threads.
It supports 256 encoders per command buffer. A plot reports skipped or invalid samples.
Empty or invalid timestamps never become synthetic durations.

On the validated M3 Max, a second timestamp attachment prevented Forge from receiving its samples.
The bridge therefore shares Forge samples when present and supplies its own samples otherwise.
Render encoders use four timestamp slots. The bridge adds no encoder boundaries, synchronization barriers, or GPU waits for stage timings.
The implementation follows the [Tracy Metal event layout](https://github.com/wolfpld/tracy/blob/v0.14.1/public/tracy/TracyMetal.hmm).
Its thread-independent contexts permit resource workers and the main thread to encode work for the same queue.

## Deeper shader profiling in Xcode

The [Metal debugger](https://developer.apple.com/documentation/xcode/optimizing-gpu-performance) provides pipeline costs, GPU counters, and shader profiling within a captured frame.
This workflow does not require changes to the rendering groups.
The [capture guide](https://developer.apple.com/documentation/xcode/capturing-a-metal-workload-in-xcode) describes the scheme configuration and capture controls.

Debug builds include shader line tables and embedded source code through FSL `--debug`.
The Metal compiler retains its normal optimization settings.
Other build configurations omit shader debug information.
Each configuration uses a separate resource directory and shader cache under `generated/<configuration>/`.

### Launch an existing Debug build

1. Build the `MooringSimulator` target in Debug with CMake or CLion.
2. In Xcode, choose **Debug > Debug Executable…**.
3. Select the built `MooringSimulator.app` bundle.
4. In the scheme editor, open **Run > Options** and set **GPU Frame Capture** to **Metal**.
5. In **Run > Arguments**, add any scene arguments for the capture.
6. Choose **Product > Run** (`⌘R`).
7. In the debug bar, click **Capture GPU Frame**.
8. Enable **Profile after Replay** in the advanced capture options.
9. Capture one frame after the scene reaches the state you want to inspect.

For the default CLion build, the app is `cmake-build-debug/MooringSimulator.app`.
This workflow does not require an Xcode project.
An existing generated Xcode project also works with the `MooringSimulator` scheme and its Debug Run configuration.
The generated scheme enables Metal frame capture.
The `MooringProfile` target runs the Tracy benchmark and exits automatically.

### Inspect the frame

1. Select the Surface encoder in the performance timeline.
2. Compare the ocean and spray pipeline costs.
3. Inspect the expensive shader with the shader cost graph and pipeline statistics.
4. Export the capture as a `.gputrace` file with **Embed performance data** enabled.

The captured pipeline costs help identify expensive shaders. Some detailed counters measure work in isolation during replay.
Those measurements do not divide the live Tracy encoder duration into additive draw timings.

The generated Debug Metal sources reside under `generated/Debug/shaders/MACOS` in the build directory.
The app bundle includes the compiled shaders and their embedded sources under `Contents/Resources/Shaders/MACOS`.

### Inspect frame pacing in Instruments

The [Game Performance template](https://developer.apple.com/documentation/xcode/analyzing-the-performance-of-your-metal-app) includes Metal System Trace and CPU scheduling data.

1. Open Instruments and choose **Game Performance** or **Metal System Trace**.
2. Select the built app as the target.
3. Click **Record** to launch the app and collect a trace.

In a generated Xcode project, **Product > Profile** (`⌘I`) opens Instruments.
That action uses the scheme's Profile build configuration, which defaults to Release.

## Memory interpretation

| Pool | Meaning |
|---|---|
| `Forge CPU` | Requested bytes from Forge allocation functions, including Jolt and Flecs allocations routed through them |
| `Jolt scratch (suballocated)` | Temporary allocations within the existing Jolt arena |
| `Metal buffers (logical)` | Buffer resource lifetimes and requested sizes |
| `Metal textures (logical)` | Texture resource lifetimes and Metal-reported allocation sizes |
| `Metal heaps (committed)` | Backing allocations from the Metal heap allocator |

These pools overlap. The scratch arena also appears in `Forge CPU`. Resources occupy the committed GPU heaps.
Their sum is not process memory usage. The **Metal / driver allocated bytes** plot provides a separate device allocation measurement.
Some texture views and external display resources report zero allocation bytes.

The allocator hooks preserve allocation, reallocation and free order under the allocator mutex.
A failed reallocation retains the original allocation. On-demand captures cannot reconstruct allocations that precede their connection.
An active allocation at the capture boundary does not establish a leak.
Foundation, driver internals and unrelated system allocations do not pass through the Forge allocator.
At application shutdown, the client drains events before thread-local storage disappears. This preserves final zone ends and deallocation events.

## Validation

Build the protocol tests:

```sh
cmake --build build/profile --config Debug --target MooringTracyTests
```

Start a capture. Then run four test rounds:

```sh
build/profile/Debug/MooringTracyTests 4 wait
build/tracy-tools/mooring-tracy-inspect build/locks.tracy --clean-memory
```

The tests cover concurrent address reuse, reallocations, alignment, recursive locks, successful and failed try-locks, and condition wakeups and timeouts.
Stage validation covers shared Forge samples and the sample buffer owned by Tracy.
The inspector checks parent containment, matching endpoints, and vertex/fragment ordering with `--gpu-stages`.
An assertion-enabled capture tool also checks Tracy's event protocol. Build the tools with `CMAKE_BUILD_TYPE=Debug` for this check.
The integration supplies a wait/obtain pair for successful try-locks, as required by the 0.14.1 debug validator.

The completed M3 Max runs include full Ultra and Balanced QA, five window sizes, and nine graphics reloads.
An optimized application also passes with fine zones and CPU callstacks.
Their traces contain no open CPU zones, invalid published GPU intervals, or outstanding tracked allocations at shutdown.
The official assertion-enabled capture tool accepts all five traces.

The [performance measurements](../build/tracy-performance-report.json) retain both normal and recording runs.
GPU timings varied strongly even with Tracy excluded, so these runs do not isolate a reliable overhead percentage.
The normal executable has no Tracy symbols. Default profiling remains disconnected until a viewer connects.

Tracy does not provide kernel scheduling traces, hardware sampling or CPU context-switch capture on this macOS target.
Those features require a supported platform or a separate native profiler.
The application has no fibers or shared-reader locks, so there are no corresponding event sources.

The [Tracy manual](https://github.com/wolfpld/tracy/blob/v0.14.1/manual/tracy.tex) describes the viewer and platform-specific capabilities.
