# Patrician III D3D11 renderer

A drop-in replacement for `ddraw_Dll.dll`, the 2D graphics library of *Patrician III*, built on Direct3D 11. It runs the game at 1920×1080 and 2560×1440 in a borderless or normal window, scales sharply to any screen size and renders the busiest city views at high FPS for a better Experience. The game executable stays unchanged on disk.

## Why a new graphics library?

*Patrician III* (2003) does all of its drawing through one library, `ddraw_Dll.dll` (the "SGL" library): every sprite, map tile, menu, text line and clip region goes through its exports. The original library draws on the CPU into DirectDraw surfaces, and the game only offers 4:3 and 5:4 modes up to 1280×1024. There is no 16:9 output, no control over scaling or windowing, and the frame rate is capped.

This project provides the same 180 exports on Direct3D 11; the few that are empty in the original are empty here too. The game keeps calling the same functions with the same arguments; the new library keeps the game's pixels in GPU textures, draws with shaders that reproduce the original blending formulas bit for bit, and presents the result through a modern swap chain. Pixel output is checked against the original library by comparison tests.

## Features

- Game resolutions 1280×1024, 1600×900, 1920×1080 and 2560×1440, selectable in the in-game options, with the HUD and info panels laid out for the wider view.
- Wide fast-forward map and widened menu backgrounds for 16:9.
- Borderless mode, or a normal window that can be moved, resized and maximized and releases the mouse.
- Sharp scaling: the game can render at one resolution and be shown at another.
- Optional uncapped frame rate and an on-screen frame counter.
- Works with the GOG and Steam releases and with the P3Modding modloader.

## Resolutions and scaling

The game itself only offers resolutions up to 1280×1024. The renderer patches the game's resolution handling in memory: the options menu lists 1280×1024, 1600×900, 1920×1080 and 2560×1440, and the game views, the top bar and the side panels are laid out for the chosen size. The choice is stored in `high_res.ini`.

Two sizes are involved:

- **Render size**: the resolution the game draws at, chosen in the options or in `high_res.ini`.
- **Screen size**: the size of the monitor (borderless) or of the window.

When they differ, the finished frame is scaled to the screen with the aspect ratio kept. A 1920×1080 game on a 2560×1440 monitor is upscaled by 4/3 to fill the screen; 2560×1440 on a 2560×1440 monitor is shown 1:1. Scaling uses an edge-aware filter: each output pixel takes the nearest game pixel, and only the one pixel that straddles the edge between two game pixels is blended. Text and diagonals stay crisp at non-integer factors such as 4/3, instead of every third pixel being doubled as with plain nearest-neighbour scaling.

The game's menus are fixed 800×600 screens. They are scaled into the render size (by 2.4 at 2560×1440) and centred; the widened backgrounds in `assets\` fill the sides.

## Frame rate

The original game never draws more than 50 frames per second: a limiter sleeps until 20 ms have passed since the previous frame (about 43 fps measured). Game time comes from the system clock, not from the frame count, so the limit only affects smoothness. `max_fps` in `high_res.ini` rewrites that limit; `0` removes it.

Without the limit, the first version of this renderer reached only about 20–28 fps in the populated city of Danzig at 2560×1440 with Details high (the animated water and about a thousand extra sprites). Profiling found the time in a few places, fixed in three passes:

| Change | Effect |
|---|---|
| Clip lists were rebuilt for every sprite; they are now cached per clip state. | fewer allocations and mapping per sprite |
| Solid fills on surfaces held by the GPU forced a full read back to the CPU and a full upload; they are now drawn on the GPU with exact integer equivalents of the original formulas. | no full-surface transfers for fills |
| Read-backs copy only the region the GPU has written since the last synchronisation. | smaller transfers |
| The animated water (Details high) locked the whole screen surface every frame. The renderer now reads the water's tile list and transfers only those tiles plus their displacement margin. | about 26 MB less transfer per frame |
| The render target and shader resources stay bound between draws. | fewer driver calls |
| The game asks to draw about 34,000 sprites per frame, of which only about 1,000 are visible. Sprites outside the visible clip region are now rejected before any setup. | most of the remaining CPU time |
| Clip rectangles are mapped once per clip state instead of per sprite; textures are resolved once when selected; a lightweight lock replaces a lock whose bookkeeping cost more than the drawing calls. | less work per call |
| Draws are queued per target, and the background copies needed by translucent sprites are issued together instead of alternating with the draws. | fewer GPU pipeline stalls |

Measured on the developer's PC with the frame counter, 2560×1440 borderless, Details high:

| Build | Danzig harbour view | Danzig city view after loading |
|---|---|---|
| Before the optimisations | about 28 fps | 19–23 fps (44–53 ms per frame) |
| After the first pass | 55–58 fps | |
| After the second pass | 100–110 fps | |
| Current | | 180–189 fps (5.3–5.6 ms per frame) |

In the same view that is eight to nine times the frame rate (+700 to +800 %), with the worst frames down from about 60 ms to about 9 ms. All optimisations produce the same pixels as before; the comparison tests against the original library still pass.

## Fixed rendering problems

- **Missing buildings, walls and mountains at the bottom edge of the city at 2560×1440.** The game's city passes walk map rows 11 pixels apart and stop in a band just below the view. At a view height of 1,398 pixels that band held only one of the two row parities, so every other column entering at the bottom edge was never drawn, and scrolling carried the gaps up the screen. The renderer widens the band after checking the original code, so every row is visited exactly once at any height.
- **Uneven text and diagonals at 1920×1080 on a 1440p monitor.** Final scaling used nearest-neighbour sampling; it now uses the edge-aware filter described above.
- **Dark lines on menu buttons under the mouse pointer.** Scaled menus are redrawn in rectangles split at the pointer. Rounding both rectangles outward gave the seam pixel to both, so translucent button fills were blended into it twice. Rectangles now map to exactly the pixels whose centres they contain.
- **Windowed mode.** The game treats its window as full screen: it moved the window to the top-left corner, kept it on top, captured the mouse and hid the cursor. In windowed mode these calls are redirected, so the window can be moved and resized and the mouse is only captured while a button is held.
- **Steam and modloader executables.** Both contain the same game build as GOG, wrapped in Steam's copy protection or extended by the modloader's import. They are now recognised by the unchanged parts of the executable.

The renderer patches a few code sites of the game in memory, such as the resolution handling, the city redraw bands and the frame limiter. Before writing, it checks the original bytes at every site. If another mod has changed one of them, the renderer does not patch, the game stops with its graphics initialisation message, and the debug output names the address.

## Supported versions

| Version | Status |
|---|---|
| GOG | supported |
| Steam | supported |
| [P3Modding modloader](https://github.com/P3Modding/p3-lib) (`Patrician3_modloader.exe`) | supported; remove the modloader's `high_res.dll` from `mods\` |

All three use the same game build. The modloader loads every file in `mods\`, whatever its extension, so renaming `high_res.dll` does not disable it. That mod patches the same code as this renderer, which replaces it; the other P3Modding mods work alongside.

## Installation

1. Back up the game's original `ddraw_Dll.dll`.
2. Extract the release zip into the game folder, next to `Patrician3.exe`. It contains:
   - `ddraw_Dll.dll`
   - `high_res.ini` (see below)
   - the folder `renderer-assets\`
3. Start the game. `Patrician3.exe /nointro` skips the intro videos.

```text
Patrician III\
  Patrician3.exe
  ddraw_Dll.dll
  high_res.ini
  renderer-assets\
    fast-forward-map\
    wqhd-side-fill\
```

When building from source, copy the contents of this repository's `assets\` folder into `renderer-assets\`. To uninstall, restore the original `ddraw_Dll.dll`.

## Configuration

`high_res.ini` next to the game executable:

```ini
[display]
width=1920
height=1080
mode=borderless
max_fps=0
show_fps=0
```

| Key | Values |
|---|---|
| `width`, `height` | 1280×1024, 1600×900, 1920×1080 or 2560×1440. Changing the resolution in the game's options updates the file. |
| `mode` | `borderless` (default) or `windowed` |
| `max_fps` | `0` removes the game's 50 fps limit, `10`–`1000` sets another limit; omit it to keep the original limit |
| `show_fps` | `1` shows frames per second, frame times, draws and transfers in the top-left corner |

## Building

Requirements: Windows, CMake 3.25 or newer and Visual Studio 2022 with the C++ desktop workload (x86 tools and a Windows SDK); Node.js for the export check. The library is 32-bit, like the game.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
node tests/test-exports.mjs
```

The DLL is written to `build\Release\ddraw_Dll.dll`. The tests need no game files. The comparison programs in `tests\` (`run-original-oracle.ps1` and the others) compare the output against the original library and need files from your own game installation.

`research/sgl-original/dispatch.json` lists the exports of the original library; the build generates the DLL's export table from it.

## License

The source code in this repository is licensed under the [Apache License 2.0](LICENSE).

The images in `assets\` are derived from the graphics of *Patrician III* and are not covered by that license; the rights of the respective owners remain unaffected. *Patrician III* is a trademark of its respective owners. This project is not affiliated with them.
