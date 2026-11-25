# V-Launcher

The part of V-Converter that hands a converted mod to Friday Night Funkin' (V-Slice) or Psych Engine and switches to the game, written in C++17 with a C API (`include/vlauncher/VLauncher.h`).

| Function | What it does |
| --- | --- |
| `vlauncher_resolve` | Turns a folder, an executable or a `.app` into the game to open: `Funkin.exe` on Windows, `Funkin` on Linux, `Funkin.app` or `Friday Night Funkin'.app` on macOS, looking one folder deep (an unzipped `funkin-windows-64bit` folder works) |
| `vlauncher_resolve_game` / `vlauncher_search_game` | The same for a chosen game: `VLAUNCHER_GAME_FUNKIN` or `VLAUNCHER_GAME_PSYCH` (`PsychEngine.exe`, `PsychEngine`, `PsychEngine.app` or `Psych Engine.app`) |
| `vlauncher_enable_psych_mod` | Turns a mod on in Psych Engine's `modsList.txt` (next to the `mods` folder), adding it at the top when it isn't listed |
| `vlauncher_search` | Tries `vlauncher_resolve` on a list of folders (Desktop, Downloads, Games, Program Files, itch's apps folder, `/Applications`…) |
| `vlauncher_mods_folder` | The game's `mods` folder (`Contents/Resources/mods` inside a macOS app), created if it is missing |
| `vlauncher_install_folder` | Copies a converted mod folder into `mods/<name>`, replacing older files, and counts what it wrote |
| `vlauncher_launch` | Opens the game detached from V-Converter in its own folder: `CreateProcessW` on Windows, `open` for a macOS app, a double `fork` + `execv` elsewhere that still reports when the game can't start |

On Android the JNI bridge (`bridge/vconverter_launcher_jni.cpp`) also asks the `PackageManager` for the launch intent of `me.funkin.fnf` (or a Psych Engine port: `me.funkin.psychengine`, `com.shadowmario.psychengine`) and starts it in its own task, or opens its store page when the game isn't installed.

```
cmake -S . -B build -DVLAUNCHER_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```
