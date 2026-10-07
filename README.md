![ZiNc EX](images/ZiNc_EX_Logo.png)
# ZiNc EX

A native Windows launcher for the ZiNc 1.1 arcade emulator (Sony ZN-1 / ZN-2 based boards: Capcom,
Taito, Namco System 11 / 12 and others).
It is a single small `.exe` with no installer and no runtime to install (plain C and the Win32 API).

ZiNc EX by Hazem Abdelghani – <https://github.com/hazem-abdelghani/ZiNc-EX>

## Installation

1. Copy `ZiNc-EX.exe` (64-bit) or `ZiNc-EX-32bit.exe` into the folder that holds `ZiNc.exe`.
2. Run it and, on first start, set the **ROMs folder** on the *General* tab.
3. Pick a game and press **Play** (or double-click the game).

The launcher creates its own files next to itself: `zinc-settings.cfg` (all launcher settings), `zinc-games.cfg` (per-game settings),
`renderer.cfg`, `zinc-input.cfg`, the `renderers/`, `profiles/` and `game-settings/` folders. Delete them to start from scratch.

## Features

**Game List**
- Every game ZiNc supports, with search (Ctrl+F; its *Close* button clears the search and hides the bar for this session; *View > Search Bar* is the remembered choice), *Favorites only* and
  *List Only Available ROMs* filters.
- *Favorite* check-box column; sortable *Favorite* / *#* / *Game* columns (default order: *#*).
- Double-click a game (or press Enter) to play it; right-click a game for *Play*, *Create Desktop Icon* and
  *Add To Favorite*. *Play* is greyed out when the game's ROM files are not in the ROMs folder.
- In the game: **Pause** pauses / resumes the emulation (and mutes the sound), **Alt+Enter** toggles fullscreen.
- Recently played games menu, window position and size are remembered.
- *Create Desktop Icon* makes a shortcut named after the game that starts `ZiNc.exe` directly with
  the right options, so keyboard and controller work without the launcher.

**Video** (every option has a tooltip)
- Renderer choice: OpenGL, Direct3D and the new **Direct3D 11** renderer.
- Resolution list (4:3 resolutions are marked), full screen, scan lines, texture filtering / quality / caching,
  colour blending, frame limiter / skipping / automatic frame-rate detection, Show FPS, dithering, **slow geometry**.
- Direct3D 11 only: **Internal resolution** (Auto by default, 1x – 4x) and **xBRZ filter**
  (off / all graphics / 2D objects only) – a real GPU port of the xBRZ pixel-art scaler.
  Also **Overscan Crop**, **Bezel Image** (a PNG frame with a transparent window; General tab), **Aspect Ratio** (stretch / 4:3 / 16:9 / pixel perfect), **Fullscreen Mode** (change the screen resolution, or a borderless window over the desktop), **V-Sync**, **3D Texture Smoothing** (bilinear smoothing of 3D textures; 2D sprites stay sharp; on by default), an **FXAA filter** and **Dither Smoothing** (reduces dither patterns baked into the game graphics).

**Audio / General**
- Sound on/off, sound filter and cut-off, surround lite, stereo exciter, rotation,
  hide the console window.

**Controls**
- Remappable input plugin (`zinc-input.znc`) with keyboard, XInput and DirectInput bindings,
  analogue-stick-as-d-pad, per-button **autofire**, two pads, and ready-made defaults.
- Controls and Combos: a right click on a Keyboard / XInput / DirectInput cell opens a menu with *Set...* and *Clear*; the key window has Delete (or a right click menu with Clear), the XInput / DirectInput windows an *Unbind* button. Direction rows have D-pad / hat bindings of their own (editable, kept when *Pad Directions* is switched off); with *Pad Directions* on, the left stick and the other hat / axes work as well.
- *Combos* tab: macros for Street Fighter EX2 Plus (its own *Defaults* button resets just this tab; 20 rows are always listed); moves are shown and typed in short words (`QCF + HP`, `D, DR, R + HP`, `~` = a short wait of one Step (30 ms by default); `Hold L` = hold a direction for the Charge time; the long names such as "Heavy Punch" work too; L / R follow the "Player Side" setting (Left = the character is on the left and looks right)) or built with the picture buttons under the Sequence box (each button writes at the caret of the Sequence text, or over the selected text; the *Legend* button explains every short name); `BN1` ... `BN6` are the Button 1 ... 6 of the Controls tab (LP MP HP LK MK HK are the same buttons), `START` the Start button. The config file keeps the short numpad notation. A double click on a combo's name renames it. Charge moves (Guile, Blanka ...) wait for their charge time (*Charge (ms)*, default 600 ms; raise it if a charge move does not come out); the check box *Count The Charge You Already Hold* (off by default) shortens that wait by the time you already hold down-back when you press the trigger.

**Per Game and Play Options**
- *Game > Game Settings*: a game can have its own renderer, rotation, window / fullscreen, resolution, Direct3D 11 scale and
  xBRZ, sound options, slow geometry, **controls profile** and trainer. What is left on *Same As Global* follows the main settings.
  The settings are kept in `zinc-games.cfg`; the game is started with its own copies of `renderer.cfg` / `zinc-input.cfg`
  (folder `game-settings`). Desktop icons use them too.
- The right-click menu (and the Game menu) also has *Play Fullscreen*, *Play Windowed* and *Play With* (a renderer, once, to try it).
- *Check ROM Set*: looks at the zip files of the game, its parent set and the BIOS and says which are missing or damaged
  (the zip directory and the checksum of every file are verified). It cannot see which file *inside* a set is missing.
- Filters by **region**, **maker** and **genre** above the list (remembered for the next start; *Reset Filters* sets them and the list order back). The region (US, JP, ASIA, WORLD) is read from the words between ( ) in the game name, *Other* when none says it; maker and genre are guessed from the name and BIOS.
- *Play With Trainer* (right-click menu and Game menu) starts the game together with the trainer program set on the General tab, for example the ZiNc 1.1 Trainer. *Start Trainer With Games* (General tab) or *Game Settings > Start Trainer* start it with every / one game; it is closed when the game ends.

**Controls and Mouse**
- *Controls* has **profiles**: save the layout under a name (arcade stick, pad, keyboard ...), pick one in the list to load it,
  choose it and press *Delete* to remove it.
- The mouse cursor is shown in the game window and hidden after 3 seconds without movement (it comes back when the mouse moves). This is done by the input plugin (`cursor_hide_ms` in `zinc-input.cfg`: 0 = never hide, -1 = leave the cursor alone).
- *File > Open Screenshots Folder* opens the SNAP folder next to ZiNc.exe. The Direct3D 11 renderer saves F5 screenshots like the
  OpenGL and Direct3D renderers do, as `SNAP\<ROM set>\ZND11001.bmp`, `ZND11002.bmp` ...

**Settings, Backup, Help**
- *OK* saves the settings (and closes the settings window when it is a window of its own), *Cancel* drops the changes; *Restore Defaults* (bottom left of the settings) restores the video, audio and system settings
  (the Controls and Combos tabs, the ROM folder, the trainer program and favorites are kept; *Dark Theme* and *Start Trainer With Games* go back to off) and saves it immediately.
- *File > Backup Settings / Restore Settings*: one zip with the settings, controls, profiles, per-game settings and the ZiNc saves (`cfg` folder).
  A restore first saves the current files as `ZiNc-EX-before-restore.zip`.
- *Help > Keyboard Shortcuts* lists the keys of the launcher and of the games. Column widths and the splitter are remembered.
- *Dark Theme* (General tab, experimental, applied at the next start).

**Settings Layout.** By default the tabs (General, Video, Audio, Controls, Combos) are in a window of their own, so the game list has the whole main window; open it with the Settings button of the toolbar, *Options > Settings…* or F9 (*Options* also has Video, Audio, Controls and Combos Settings, which open that tab). *Classic Layout* (a check box on the General tab, applied with OK; also in the View menu) puts the tabs on the right of the game list instead. The choice and the position of the settings window are remembered.

**Game List Columns.** Right-click the title of the game list to show or hide its columns (Favorite, #, Info; Game always stays). Info is hidden by default. The choice is remembered; at least one column always stays.

**View Menu.** *View > Theme* or the *Theme* box of the General tab (System, Light, Dark; applied at the next start, System follows the Windows app colour mode), *Favorites Only*, *List Only Available ROMs*, *Show Toolbar*, *Show Search Bar* and *Show Filter Bar* (the toolbar, the search row and the filter row), *List Columns* (#, Status, Favorite, Game, Hardware, Year, Info; Game always stays; Year and Info are hidden by default; Status shows a green tick when the ROM set is there and a red cross when it is missing, Hardware is the arcade board such as Sony ZN-1 System or Namco System 11), *Classic Layout* (checked: the settings are in the main window; unchecked: in a window of their own) and *Reset Window Size*. The theme moved here from the General tab.

**Toolbar.** Under the menu bar: Refresh (F5), Favorites Only | Play, Stop, Fullscreen (a switch for the Fullscreen setting) | Settings (General tab), Video, Controls, Combos (the tab of the settings). The icons are drawn in the colour of the theme, so they fit the light and the dark theme. *Create Desktop Icon* is in the Game menu and in the right-click menu of a game.

**Launcher Behaviour.** *General > When A Game Starts*: keep the launcher open (default), minimize it while the game runs (it comes back when the game ends), or close it (the game and its trainer go on without it).

**Per-Game Bezels.** Put a PNG named after the game set in a `bezels` folder next to ZiNc.exe (for example `bezels\sfex2.png`; a clone such as `sfex2j` also finds the bezel of its parent `sfex2`). With the Direct3D 11 renderer that game uses it instead of the bezel of the General tab. The *Browse* button of the Bezel Image starts in that folder. *Game > Game Settings* has a *Bezel (D3D11)* choice as well: Same As Global (the automatic file or the General tab bezel), None, or any PNG of the `bezels` folder for that one game.

**Game Settings Tabs and Toolbar.** *Game > Game Settings* is split in tabs: General (renderer, rotation, window, resolution, bezel), Video (Direct3D 11: fullscreen mode, aspect ratio, internal resolution, xBRZ, 3D texture smoothing, FXAA, dither smoothing, V-Sync, overscan crop, slow geometry), Audio and Other (controls profile, trainer). The toolbar has a *List Only Available ROMs* switch next to the favorites star and an *Audio Settings* button next to Video.

**Safer Lists and Bindings.** The columns of the game list, the key bindings list and the combos list cannot be dragged narrower than 64 pixels (32 for the Favorite, #, Status and Year columns), so they never vanish. A keyboard key can belong to one action only; a pad button can belong to one action of a player (Player 1, Coin 1, Test and Service use the first controller, Player 2 and Coin 2 the second). Binding one that is taken asks whether to move it: *Yes* takes it from the other action (which is left unbound), *No* keeps the old binding. The sound filter and fullscreen are on by default.

## Frontends (LaunchBox, Pegasus, RetroBat, Playnite, ...)

ZiNc EX can be started by other frontends from the command line:

```
ZiNc-EX.exe --game <set name or ROM file> [--fullscreen | --windowed]
```

- The game is given by its ZiNc set name (`sfex`, `tekken`...) or by the path of its ROM zip (`D:\roms\sfex.zip`); the
  extension and the folder are ignored, and so is upper / lower case. A bare ROM path without `--game` works too.
- It starts the game with your global settings **and the game's own settings** (Game Settings, controls profile, bezel...),
  without showing the launcher window, **waits until the game is closed** and then exits, so the frontend knows when to come back.
- `--fullscreen` / `--windowed` change the window mode for this start only (nothing is written to your settings).
- Exit code: `0` played, `2` unknown game, `3` ROM files missing, `4` ZiNc.exe could not be started, `5` no game given.
  The reason is written to `ZiNc-EX_launch.log` beside the program.
- Arcade clones need their own zip like in ZiNc (the parent and BIOS zips must be in the ROMs folder as well).

How to set it up (the names of the menus change a little between versions):

| Frontend | Setup |
|---|---|
| **LaunchBox / BigBox** | *Tools > Manage > Emulators > Add*: path to `ZiNc-EX.exe`, default command-line parameters `--game "%romfile%" --fullscreen`. Add a platform (e.g. *Arcade*) that uses it and import the ROM zips. Untick auto-extract / "ROM is in a zip" options: the zip is passed as it is. |
| **Pegasus** | In `metadata.pegasus.txt` of your ZiNc collection: `collection: ZiNc Arcade`, `extension: zip`, `launch: "C:\ZiNc\ZiNc-EX.exe" --game "{file.path}" --fullscreen` |
| **RetroBat / EmulationStation / ES-DE** | Not built in: add a custom system with `<extension>.zip .ZIP</extension>` and `<command>"C:\ZiNc\ZiNc-EX.exe" --game "%ROM%" --fullscreen</command>` (RetroBat: in its `es_systems` configuration; ES-DE: in a custom `es_systems.xml`). |
| **Playnite** | *Add Emulator > Custom*: executable `ZiNc-EX.exe`, arguments `--game "{ImagePath}" --fullscreen`, then import the ROM folder with that profile. |
| **Steam ROM Manager (Steam / Big Picture)** | Executable `ZiNc-EX.exe`, command-line arguments `--game "${filePath}" --fullscreen`. |
| **Attract-Mode** | In the emulator config: `executable ZiNc-EX.exe` and `args --game "[romfilename]" --fullscreen` (the name without extension is enough). |
| **HyperSpin / RocketLauncher, GameEx and others** | Anything that can run a program with the ROM name or path: `ZiNc-EX.exe --game <rom> --fullscreen`. Or use *Game > Create Desktop Icon* for a shortcut per game. |

The command line itself is tested; the frontend setups above follow the frontends' documentation and have not been tried in each frontend.

## Direct3D 11 Renderer

`renderer-d3d11/` is a new renderer plugin for ZiNc (Direct3D 11).
It renders at up to 4x the native resolution, can apply the xBRZ filter to 2D objects only and handles
the tiled textures used by the System 12 / wide-layout games. It is built into the launcher and copied
to `renderers/Direct3D11/` on start. See [renderer-d3d11/README.md](renderer-d3d11/README.md).


## Building

| Part | Folder | Notes |
|---|---|---|
| Launcher | `gui-c/` | plain C + Win32, see [gui-c/README.md](gui-c/README.md) |
| Input plugin | `input-plugin/` | C, 32-bit DLL (`.znc`), see its README |
| Direct3D 11 renderer | `renderer-d3d11/` | C, 32-bit DLL (`.znc`) |

The two plugins are embedded in the launcher (`gui-c/zinc-input.znc`, `gui-c/d3d11.znc`);
rebuild them first and copy them over if you change their source.

### Building on Windows

No Visual Studio is needed. The easiest way is [Zig](https://ziglang.org/), whose bundled C compiler cross-builds the 32-bit and the 64-bit program.

1. Install [Python 3](https://www.python.org/downloads/) (tick *Add python.exe to PATH*), then open a Command Prompt and run:
   ```
   python -m pip install ziglang
   ```
2. Get the source (`git clone https://github.com/hazem-abdelghani/ZiNc-EX` or download the zip from GitHub and unpack it).
3. Build the launcher:
   ```
   cd ZiNc-EX\gui-c
   build.bat
   ```
   This produces `ZiNc-EX.exe` (64-bit) and `ZiNc-EX-32bit.exe` in the `gui-c` folder. Copy one of them next to `ZiNc.exe`.
4. Only if you changed a plugin: build it first (32-bit, because ZiNc is a 32-bit program), copy the result into `gui-c`, then run `build.bat` again.
   ```
   cd ZiNc-EX\input-plugin
   python -m ziglang cc -target x86-windows-gnu -O2 -shared -o zinc-input.znc zinc_input.c zinc_input.def -luser32 -lgdi32 -lole32 -luuid
   copy /y zinc-input.znc ..\gui-c\zinc-input.znc

   cd ..\renderer-d3d11
   python -m ziglang cc -target x86-windows-gnu -O2 -shared -o renderer_d3d11.znc d3d11gpu.c d3d11gpu.def -luser32 -lgdi32 -lwinmm -ld3d11 -ldxgi -ldxguid
   copy /y renderer_d3d11.znc ..\gui-c\d3d11.znc
   ```

If you prefer MSYS2 / MinGW-w64 (open the *MinGW x64* shell and install `mingw-w64-x86_64-gcc` and `mingw-w64-i686-gcc`), the launcher builds with
`windres app.rc -O coff -o app.o` and then `gcc -Os -s -municode -mwindows -o ZiNc-EX.exe <all .c files> app.o -luser32 -lgdi32 -lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid -luxtheme -lwinmm -ladvapi32`
(use `i686-w64-mingw32-gcc` for the 32-bit program). The list of source files is in `gui-c/build.bat`.

### Building on Linux or macOS

```
cd gui-c
sh build.sh      # needs "pip install ziglang"; produces ZiNc-EX.exe and ZiNc-EX-32bit.exe
```

## Credits

ZiNc 1.1 and its plugins belong to their authors (see `readme-ZiNc.txt`). This project is only a launcher
and a set of extra plugins for it.

