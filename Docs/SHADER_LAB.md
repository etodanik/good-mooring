# Shader Lab

Shader Lab is available in the Debug app. It uses the Forge shader compiler and reload client.

## Start a session

1. Build the `MooringSimulator` target in Debug.
2. Start the app.
3. Press **F3**, or select **F3 Shaders** in the bottom bar.
4. Select **Functions**, **Resources**, **Pipelines**, or **Scene probes**.
5. Select an entry in the browser. Use the search field to filter names or shader files.
6. Edit the shader in your editor.
7. Select **Reload shaders**, press **Ctrl+F2**, or press **Ctrl+S** in the app.

**Auto reload on save** watches the files under `Mooring/Shaders`. The app compiles changed shaders after a short pause between saves.

The app starts its own local reload host. The status line shows compilation progress and errors. **Build log** opens the compiler details.
The app keeps the previous shaders until compilation succeeds.
Shader reload replaces graphics and compute pipelines. The simulation, camera, textures, buffers, and ocean history remain available.

Changes to shared `*.srt.h` files require a rebuild and restart. The reload host rejects these changes because CPU and GPU layouts must agree.

The command-line argument `--shader-lab` opens this window at startup.

## Window layout

Shader Lab has its own movable, resizable window. The title-bar **X** closes it; the bottom bar reopens it. **F2** opens Sea & Weather independently. **F3** opens or hides Shader Lab.

- The left pane lists registered entries. Each category keeps its search and last selection.
- The center pane shows the preview and selected sample values.
- The right pane contains display settings, input controls, parameters, and source details. It scrolls independently of the preview.

On narrow displays, **Show controls** and **Back to preview** switch between the inspector and preview.
Each entry retains its settings during the session. **Reset this preview** restores its defaults.
**Ctrl+F1 Pause/Resume** in the bottom bar controls simulation time. Preview time has a separate animation control.

## Function canvases

A preview wrapper calls the real shader function with explicit inputs. The GPU evaluates the wrapper in a separate preview pass.
This pass runs while the canvas is visible. Existing ocean render passes retain their grouping.

Each wrapper returns a `float4`. The canvas shows that return value. It does not discover intermediate buffers inside a function.
The raw value comes from GPU readback after the frame fence completes. Display ranges and color ramps do not change that value.

Add wrappers to a file such as `Mooring/Shaders/Water.preview.fsl`:

```c
#include "WaterLighting.h.fsl"

float4 previewMyHash(ShaderPreview2D preview)
{
    return previewScalar(hashWater(preview.coordinate));
}
SHADER_PREVIEW_2D(previewMyHash, "My hash", PREVIEW_INTEGER_GRID);
```

The Debug build generates entry points and the preview registry. A shader reload discovers new registrations without an app rebuild.
Registration labels and wrapper names must be unique. Existing wrappers in `Water.preview.fsl` provide examples with actual water resources.

| Registration | Context | Output |
| --- | --- | --- |
| `SHADER_PREVIEW_1D` | `ShaderPreview1D` with a scalar coordinate | Curve of the selected component |
| `SHADER_PREVIEW_2D` | `ShaderPreview2D` with a `float2` coordinate | Image across an input plane |
| `SHADER_PREVIEW_3D_SLICE` | `ShaderPreview3D` with a `float3` coordinate | XY, XZ, or YZ slice |

All contexts also contain `time` and `parameters`, a user-controlled `float4`.

`PREVIEW_CONTINUOUS` evaluates fractional coordinates. `PREVIEW_INTEGER_GRID` floors the coordinates before the function call.
The input mode is a canvas setting, independent of its dimensions. For example, a hash uses a 2D canvas with integer coordinates.

### Inspect a function

1. Select the wrapper under **Functions**.
2. Use the inspector on the right.
3. Set the input origin and span under **Input domain**.
4. Set the wrapper parameters under **Time and parameters**. Parameter hints appear above these controls.
5. Select the output component and display range. **Fit output range** uses the finite values from the latest GPU preview.
6. Adjust **Zoom** and **Pan** to inspect part of the input domain.
7. Click the image, or adjust the sample column and row, to read a value.

The preview shows the input coordinate, preview time, and raw output for the selected sample. A crosshair marks the sample on images.
Use **Column** and **Row** to enter a sample index. Numeric controls also accept typed values.
**Animate preview time** advances the wrapper time independently of the simulation. **Freeze preview** retains the preview and disables its input controls.
The image becomes magenta for a selected output that contains NaN or infinity. The signed color ramp shows negative values in blue and positive values in red.

The initial wrappers cover water hash, noise, Fresnel, scattering, cloud density, foam pattern, and `sampleWaterLevel`.
The cloud wrapper samples world coordinates. A slice outside the cloud layer can contain only zeroes.

## GPU resources and pipelines

**Resources** shows registered textures and buffers after the scene render. It includes the current and previous ocean fields, normals, whitewater, particles, sky, and scene targets.

Structured buffers have explicit layouts. The panel shows their channel meanings, element size, ocean band, and mip level.
Float, half-float, and unsigned integer fields share the same viewer. The viewer converts integers to floats, so integers above 16,777,216 can lose precision.

Sampled textures support mip selection. Volume textures support slice selection. Tile-only depth attachments provide metadata because their contents do not survive the render pass.
The browser does not copy tile-only attachments or change their storage mode.

**Pipelines** shows shader stages, depth and blend settings, and compute thread-group dimensions.
Each resource button opens the resource viewer. **Back to pipeline** returns to the originating pipeline.
The bindings describe the most recent use of that pipeline.
This view is not a frame graph or a record of every dispatch.

## Scene probes

A scene probe shows a value at the actual water fragment. Place the marker inside `Water.frag.fsl` `PS_MAIN`, after the function call:

```c
float4 pattern = foamPattern(surfacePosition, flowVelocity, surfaceSlope);
SHADER_PREVIEW("Foam / pattern in scene", pattern);
```

The existing **Scene probes** entry uses this marker. Select **Show probe and hide lab** to inspect it across the scene.
Press **F3** to return to the controls. **Show probe with lab open** keeps the window visible.
Select **Restore normal shading** in the lab, or **Ctrl+F4 Beauty** in the bottom bar, to end the preview.
Hiding the lab keeps an active scene probe visible. Selecting a different category restores normal shading.
The probe keeps the original function call and observes its result. A checkerboard marks fragments that do not reach the marker.

The marker accepts a scalar or `float4`. Pack shorter vectors explicitly:

```c
SHADER_PREVIEW("Water / slope", previewVector2(surfaceSlope));
SHADER_PREVIEW("Water / normal", previewVector3(surfaceNormal));
```

Scene probes currently support the water fragment entry point. Function canvases provide isolated previews for helpers, hashes, curves, and volume functions.
Release builds erase the scene markers and omit the preview shaders.

## Validation

Run the generator tests:

```sh
python3 Tools/Mooring/test_shader_previews.py
```

Run the GPU smoke test from the repository root:

```sh
MTL_DEBUG_LAYER=1 TRACY_PORT=8087 \
  cmake-build-debug/MooringSimulator.app/Contents/MacOS/MooringSimulator --shader-lab-qa
```

The smoke test checks function output, structured buffer formats, packed ocean mips, textures, volume slices, and shader reload.
It checks that render targets survive the reload. A successful run prints `Shader Lab QA PASSED` and exits.
