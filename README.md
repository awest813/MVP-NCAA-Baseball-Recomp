# MVP 06 NCAA Baseball - XBE Recomp

An unofficial native PC port of the original Xbox version of MVP 06 NCAA Baseball, built by static recompilation on [XboxXBERecomp](https://github.com/TGP482/XboxXBERecomp).

> [!NOTE]
> This project does not include any game assets. You must provide the files from your own legally acquired copy of the game to install or build.

## Status

Playable: the intro movie, menus, controller input and full games run, with UI text and the pitch camera's depth of field rendering correctly. Gameplay presents at 60 FPS and the game's logic runs at real-time speed.

Not working: Xbox Live sign-in (the service is gone; the attempt times out after about 15 seconds).

## Installation

- Put your game files in the `assets` folder. This can be an ISO, or the extracted data files. You can also skip this and pick the ISO or `default.xbe` wherever it is from the launcher.
- Run `MVP06.exe`. The first start compiles the game's code for your PC; this only happens once.
- Recomp settings can be adjusted in the launcher or in `settings.toml` beside the exe.

## Features

### Controller
Supports Xbox, PlayStation and Switch controllers, with hot plugging and rumble.

### Launcher
Each start opens a launcher before the game: the game to play (the extracted files or ISOs found in `assets`, or **Browse** for one anywhere) and the display, frame rate and texture settings. Use the arrow keys, the mouse or a controller's d-pad; Enter or A plays, Esc or B exits. With **Show this launcher at start** off, the game starts at once; hold Shift while starting it to bring the launcher back.

### Display
- **Fullscreen** switches the monitor to the chosen resolution, at the desktop's refresh rate where the monitor offers it there, and gives the desktop back while you alt-tab away. **Borderless** covers the screen at the desktop's resolution.
- **Alt+Enter** toggles between windowed and the fullscreen or borderless mode you were in.
- The mouse pointer hides over the game after two seconds without moving, and the display does not sleep while the game is in front.

### Miscellaneous
- **Boot Splash** - Place a `splash.png` (or `.bmp`) beside the exe to display a splash screen at launch.

## System Requirements

- Windows 10 64-bit
- A GPU with Direct3D 12 support

## Building

1. Clone this repository with [Git](https://git-scm.com/). The SDK is included in `sdk/`; no submodules are needed.
2. Place your extracted game data in the `assets` folder: `default.xbe` must be there, with the game's folders beside it.
3. Install Visual Studio 2022 with **Desktop development with C++**, **C++ Clang tools for Windows** and **C++ CMake tools for Windows** (`build.bat` offers to add the last two).
4. Run `build.bat`. The first build recompiles the game and takes a while. The exe is written to `out\build\release`.

## Layout

| Path | What it is |
| --- | --- |
| `config/game.toml` | The XBE's function map from `xbe_symbols`, plus hand-found D3D/XAPI functions with no XDK 5849 signature |
| `src/game/hooks.cpp` | Game-specific replacements of recompiled functions |
| `src/common/game_info.cpp` | Titles and runtime hooks |
| `sdk/` | XboxXBERecomp (MIT, TGP482) at upstream `f7b13fd`, vendored with the MVP 06 fixes below; only what the build needs |

### SDK changes for MVP 06

- **UI text:** EA's font code pushes raw pushbuffer commands and then restores the previous shaders. The runtime now processes queued commands before every D3D state change. The current shader handles are also mirrored where `GetVertexShader`/`GetPixelShader` read them.
- **Depth of field:** the game reads its depth buffer as an A8R8G8B8 texture and looks up the blur amount by those bytes. The depth target is converted to the console's byte layout for the draws that sample it.
- **Black 3D screens:** the sampler LOD bias is clamped to Direct3D 12's range. MVP 06 sets -100.5, and D3D12 removed the device.

## Debugging aids

Each is enabled by an environment variable:

| Variable | Effect |
| --- | --- |
| `XBE_CAPTURE_DIR=cap` | F3 saves the next frame (every draw, target, texture and shader) under `cap/` |
| `XBE_CAPTURE_AT=30,60` | Also captures automatically at those seconds after launch |
| `XBE_SNAP_EVERY=2` | Saves just the presented frame every 2 seconds under `cap/snaps` |
| `XBE_PAD_SCRIPT=44:UP,47:A` | Presses controller buttons at set seconds, for unattended test runs |
| `XBE_D3D12_DEBUG=1` | Direct3D 12 debug layer and DRED; a removed device is reported with its cause |
| `XBE_PERF=1` | Per-second frame timing (needs a verbose build) |

## History

An earlier LLE port on the xboxrecomp toolkit (NV2A pushbuffer emulation with a D3D11 renderer) ran at 30–40 FPS but stalled for about a minute between pitches. This XboxXBERecomp port replaces it. The old version lives on in [awest813/xboxrecomp](https://github.com/awest813/xboxrecomp) under `mvp06/`.

## Credits

- [XboxXBERecomp](https://github.com/TGP482/XboxXBERecomp) by TGP482: the recompiler and runtime this is built on (MIT, see `sdk/LICENSE`).
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) for the overridable function layout and instruction hooks.
- [Zydis](https://github.com/zyantific/zydis) for decoding x86 instructions, [toml++](https://github.com/marzer/tomlplusplus) for the configs, [Dear ImGui](https://github.com/ocornut/imgui) for the settings overlay and [SDL](https://github.com/libsdl-org/SDL) for the window, input and controllers.
