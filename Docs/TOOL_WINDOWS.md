# Tool windows and command bar

The bottom bar shows window toggles. Hold **Ctrl** to show actions instead. Counters remain visible in both modes.
Buttons and counters use fixed widths, so labels and modifier changes do not move adjacent items.
Rows wrap when the window is too narrow. The bar height follows its text rows.

| Key | Window | With Ctrl |
| --- | --- | --- |
| F1 | Skipper & stations | Pause/resume simulation |
| F2 | Sea & Weather | Reload shaders (Debug) |
| F3 | Shader Lab (Debug) | Capture view |
| F4 | Session | Restore Beauty shading |
| F5 | Diagnostics | Toggle uncapped FPS |
| F10 | Hide/restore all UI | Hide/restore all UI |

On Mac keyboards that use the top row for hardware controls, hold **Fn** as well: **Fn+Ctrl+F2** reloads shaders.
The app handles its Ctrl+F-key shortcuts before macOS focus navigation while the game window is active.

**F10** hides every tool window and the bottom bar. It retains open windows, pending edits, and simulation pause state.
Press **F10** again to restore them. A window shortcut also restores the UI.

**Session → Uncapped FPS** and **Ctrl+F5** disable both the 30 fps app timer and VSync.
Turning this off restores the 30 fps limit and VSync. The setting remains active through shader reloads and window resizing.

**Control+Command+F** toggles fullscreen on macOS, including when the tools UI is hidden.
The **View → Enter/Exit Full Screen** menu and **Session → Display** provide the same engine fullscreen toggle.

Windows retain their position and size during the session and fit above the command bar when the app resizes.
Closing a lab retains its settings and pending edits. The app bundles Inter Regular 4.1 and its license.

## Sea & Weather

- **Sea state:** presets apply immediately and reset the boat. Edited values wait for **Apply sea + reset boat**.
- **Weather & light:** live cloud, rain, sun, exposure, and bloom controls.
- **Water & foam:** live surface, scattering, foam, and spray controls.
- **Quality:** resource sizes wait for **Apply resource sizes**. Sample budgets and filtering apply live.
- **Inspect effects:** search scene views, adjust display gain and camera angles, or capture the view.
- **Physics:** view vessel motion and the righting-arm curve, release the boat at a heel angle, or emit a wave packet.

The status and action area stays visible while controls scroll. **Discard** restores the active sea values or resource sizes.
Quality presets apply live settings immediately and stage resource sizes for Apply.
An active Shader Lab scene probe remains visible until you select another effect or restore Beauty.

## Skipper & stations

Select a station to inspect it with the camera. **Send skipper** starts travel to that station.
Travel status shows the destination and remaining time. At the helm, select **Take helm controls** to operate the wheel and engines.

The bottom bar shows helm keyboard controls while the helm is active: **A/D** for the wheel and **W/S** for throttle.
Text and numeric editing take priority over these controls. **Follow boat** leaves helm operation and restores the overview camera.
Closing the skipper window also leaves helm operation.

**Session** contains exercise restart, vessel selection, and interaction difficulty. Opening Session pauses simulation.
Beginner mode pauses during station decisions; Intermediate mode uses quarter speed; Advanced mode stays live.
Sending the skipper ends the decision pause so travel can proceed. Manual Pause remains in effect until you select Resume.

## Frame counters

The bottom bar shows smoothed milliseconds and frames per second. Hover a counter for its meaning.

| Counter | Measurement |
| --- | --- |
| Frame | Frame interval, including pacing |
| Sim | CPU simulation work |
| Update | CPU application update, including simulation and tool UI |
| Render | CPU command encoding, submission, and presentation |
| Wait | Waiting for a drawable and the GPU fence, plus completed preview readback |
| GPU | Existing whole-frame GPU measurement |

These values are not additive. Update includes Sim, and GPU execution overlaps CPU work.
A dash means that a measurement is unavailable. The counters do not change render-pass grouping.
