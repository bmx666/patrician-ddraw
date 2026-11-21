# Patrician III D3D11 renderer

A drop-in replacement for `ddraw_Dll.dll`, the 2D graphics library of *Patrician III*, built on Direct3D 11. It runs the game at modern resolutions in a borderless or normal window, with sharp scaling and high frame rates, and leaves the game executable unchanged on disk.

## Features

- Game resolutions 1280×1024, 1600×900, 1920×1080 and 2560×1440, selectable in the in-game options, with the HUD laid out for the wider view.
- Wide fast-forward map and widened menu backgrounds for 16:9.
- Borderless mode, or a normal window that can be moved, resized and maximized and does not keep the mouse.
- Sharp scaling when the render size differs from the screen size: whole pixels stay crisp, only the seam pixels are blended.
- Optional uncapped frame rate and an on-screen frame counter.
- Fast: around 180 fps in the populated city of Danzig at 2560×1440 with Details high on the developer's PC.

The renderer patches a few code sites of the game in memory, such as the resolution handling and the frame limiter. Before writing, it checks the original bytes at every site. If another mod has changed one of them, the renderer does not patch, the game stops with its graphics initialization message, and the debug output names the address.

## Supported versions

| Version | Status |
|---|---|
| GOG | supported |
| Steam | supported; the game must be started from its Steam library installation with the Steam client running, because Steam's copy protection rejects copies elsewhere |
| [P3Modding modloader](https://github.com/P3Modding/p3-lib) (`Patrician3_modloader.exe`) | supported; remove the modloader's `high_res.dll` from `mods\` |

All three use the same game build. The modloader loads every file in `mods\`, whatever its extension, so renaming `high_res.dll` does not disable it. That mod patches the same code as this renderer, which replaces it; the other P3Modding mods work alongside.

## Installation

1. Back up the game's original `ddraw_Dll.dll`.
2. Copy into the game folder, next to `Patrician3.exe`:
   - `ddraw_Dll.dll` (from a release or your own build)
   - `high_res.ini` (see below)
   - the folder `renderer-assets\`, with the contents of this repository's `assets\` folder
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

To uninstall, restore the original `ddraw_Dll.dll`.

## Configuration

`high_res.ini` next to the game executable:

```ini
[display]
width=2560
height=1440
mode=borderless
max_fps=0
show_fps=0
```

| Key | Values |
|---|---|
| `width`, `height` | 1280×1024, 1600×900, 1920×1080 or 2560×1440. Changing the resolution in the game's options updates the file. |
| `mode` | `borderless` (default) or `windowed` |
| `max_fps` | `0` removes the game's 50 fps limit, `10`–`1000` sets another limit; omit it to keep the original limit. The game's speed does not depend on the frame rate. |
| `show_fps` | `1` shows frames per second and frame times in the top-left corner |

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
