# gui-c – the launcher (plain C + Win32)

`ZiNc-EX.exe` is written in plain C against the Win32 API: no framework, no runtime, no installer.
Every control (list view, tabs, combo boxes, tool tips, status bar) is a standard Windows control.

| File | What it does |
|---|---|
| `main.c` | main window, menus, layout, game list, play / stop, settings <-> widgets |
| `ui.c` | control helpers and the small modal windows (key capture, pad button picker, About) |
| `controls.c` | the *Controls* tab (key / XInput / DirectInput bindings, autofire) |
| `combos.c` | the *Combos* tab (move macros) |
| `input.c` | input plugin settings: `zinc-input.cfg`, key names, pad detection, combo loadouts |
| `settings.c` | `zinc-settings.cfg`, `renderer.cfg`, ZiNc command line, built-in plugin installation |
| `games.c` | `ZiNc.exe --list-games`, ROM availability, shortcut names, desktop icon |
| `tips.c`, `combo_data.c` | tool tip texts, move lists of the SF EX2 Plus characters (data only) |
| `embed.c` | pulls `zinc-input.znc` and `d3d11.znc` into the exe (`.incbin`) |
| `icon_data.c`, `gen_icons.py`, `icons/` | the pictures of the sequence buttons on the Combos tab: the PNG files in `icons/`, turned into `icon_data.c` by `python gen_icons.py` (needs `pip install pillow`; run it again when a PNG changes) |
| `app.rc`, `app.manifest`, `icon.ico` | icon and the manifest (visual styles, DPI awareness) |

## Build

    python3 -m pip install ziglang
    sh build.sh        # Linux / macOS / Git Bash
    build.bat          # Windows Command Prompt (see the main README, "Building on Windows")

produces `ZiNc-EX.exe` (64-bit) and `ZiNc-EX-32bit.exe`. Any mingw-w64 toolchain works as well
(see the comments in `build.sh`).

`zinc-input.znc` (from `../input-plugin`) and `d3d11.znc` (from `../renderer-d3d11`, built as
`renderer_d3d11.znc`) are copied here and built into the exe; copy fresh builds over them after changing a plugin.
