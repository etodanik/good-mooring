# Water QA

Water QA is a local capture workflow for the Mooring renderer. Captures,
review sheets, logs, and comparison reports are temporary build artifacts. The
repository does not store screenshots or run logs.

## Configure and run

Configure and build the game from the repository root:

```sh
cmake -S . -B build/macos -G Xcode -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build/macos --config Debug --target MooringSimulator
```

The CMake targets remove the previous `QA/` directory before each capture:

| Target | Capture |
| --- | --- |
| `MooringQA` | Balanced quality, all fixed diagnostic and beauty views. |
| `MooringQAUltra` | Ultra quality, all fixed diagnostic and beauty views. |
| `MooringQAMotion` | Ultra storm motion for the configured number of simulated seconds. |
| `MooringQAResize` | The resize fixture. |
| `MooringQAReload` | The resource and shader reload fixture. |

Run a target with:

```sh
cmake --build build/macos --config Debug --target MooringQAUltra
```

Set `-DMOORING_QA_SECONDS=<seconds>` when configuring to change the motion
fixture duration. Use the application flags in `Mooring/MooringApp.cpp` for
focused local investigations.

## Capture inspection

The full capture targets run `Tools/Mooring/check_water_captures.py` after the
application exits. The checker needs Pillow:

```sh
python3 -m pip install Pillow
```

To inspect a capture manually or rerun the checker, locate the generated
directory below the application build directory:

```sh
find build/macos -type d -name QA -print
python3 Tools/Mooring/check_water_captures.py \
  build/macos/Debug/QA --preset ultra
```

The checker produces motion, effects, and beauty review sheets in the same
directory. It fails when adjacent motion frames have a large lower-left
channel-mean jump. This is a regression signal, not a substitute for visual
review. Inspect the waterline, crest shape, foam continuity, reflections,
spray, mist, and dock interaction at both the overview and low-angle cameras.

## Acceptance notes

- Fixed diagnostic captures must start successfully and cover every registered
  scene/effect without missing files.
- Paused diagnostic views must not change while the screenshot is taken.
- Motion must preserve crest-localized foam, dark troughs, and coherent wake
  transport between frames.
- Resize and resource reload must restore stable resource counts and preserve
  the live simulation state.
- Performance claims require an unthrottled run with the profiling workflow in
  [TRACY.md](TRACY.md); screenshot runs alone do not establish frame time.

Clean local generated resources and QA output with:

```sh
cmake --build build/macos --config Debug --target MooringClean
```
