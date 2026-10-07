# zinc-input.znc - keyboard + XInput + DirectInput plugin for ZiNc

Drop-in replacement for `controller.znc` (same `ZN_JammaOpen/Close/Read` exports and per-game bit
layouts, extracted from the original plugin by `gen_tables.py`). ZiNc-EX embeds it and passes
`--controller=<path>\zinc-input.znc` automatically; to use it by hand:

    ZiNc.exe 1 --controller=C:\full\path\zinc-input.znc

Settings are read from `zinc-input.cfg` in the same folder (the GUI's Controls tab writes it).

## Default layout
| | Directions | Buttons 1-6 | Start |
|---|---|---|---|
| P1 | W A S D | Numpad 4 5 6 1 2 3 | Enter |
| P2 | Arrow keys | U I O J K L | Y |

Coin 1 = Right Shift, Coin 2 = `H`, Test = `F4`, Service = `F7`. **Numpad keys need NumLock on.**
Pads (XInput, both players): X Y RB A B RT = buttons 1-6, Start = Start, Back = Coin. D-pad / left stick (XInput) and
hat / X-Y axes (DirectInput) always drive the directions. Player 1 uses the first detected pad,
player 2 the second (XInput pads are listed first; XInput devices are not double-counted as DirectInput).

## cfg syntax
    deadzone=50          ; stick/trigger/axis threshold, percent
    xinput=1             ; 0 disables XInput
    directinput=1        ; 0 disables DirectInput
    analog=1             ; 0 = pad directions only via explicit bindings
    logs=0               ; 1 writes zinc-input.log next to the plugin (off by default; the GUI's "Enable Logs")
    pad1=0               ; 0 auto, none, or n = n-th detected pad
    p1_b1=K:NUM4,X:X,J:B1
    coin1=K:5,X:BACK,J:B9
Roles: `p1_`/`p2_` + `up down left right b1..b6 start`, plus `coin1 coin2 test service`.
Tokens: `K:` key (letters, digits, `NUM0-9`, `F1-12`, `UP`, `SPACE`, `LSHIFT`, `VK<code>` ...),
`X:` A B X Y LB RB LT RT START BACK LS RS DPAD_* LS_* RS_*, `J:` `B1..B32`, `POV_UP/DOWN/LEFT/RIGHT`,
`AXIS_X-`/`AXIS_X+` (X Y Z RX RY RZ).

## Build
    pip install ziglang
    python gen_tables.py ../controller.znc          # regenerate tables.h
    python -m ziglang cc -target x86-windows-gnu -O2 -shared -o zinc-input.znc zinc_input.c zinc_input.def -luser32 -lgdi32 -lole32 -luuid
Tests (Wine, 64-bit build with injected input): see `test.c` / `test.def`, built with `-DZN_TEST`.

## Combo macros
A trigger (key / XInput / DirectInput) plays a move sequence for a player. cfg (slots 1-20):

    combo_step_ms=30          ; length of each step (default 30)
    combo_charge_ms=600       ; hold time of charge steps (4*C)
    combo1=K:F1               ; trigger bindings, same syntax as above
    combo1_seq=236+HP         ; sequence
    combo1_player=1           ; 1 or 2 (also picks which pad the trigger is read from)
    combo1_face=R             ; R / L = which way the character faces (mirrors forward/back); the GUI shows it as "Player Side": Left side = faces right (R), Right side = faces left (L)

Sequence notation: numpad directions (`2` down, `6` forward, `4` back, `236` = quarter-circle forward,
`623` = dragon-punch motion, `632147` = 360 rotation), buttons `LP MP HP LK MK HK` (= buttons 1-6, also written `BN1` ... `BN6` or `B1` ... `B6`), `ST` / `START` (the Start button),
`+` presses together (`236+HP`, `236236+LP+MP+HP`), `~` is a one-step pause (`6 ~ 6` = dash),
`4*C` holds that direction for the charge time (`combo_charge_ms`, default 600; charge moves: `1*C 6+HP` - charging down-back counts as back and down and keeps the character crouching in place),
`4*2000` holds it for an explicit 2000 ms. Pressing the same button twice
in a row inserts a release step automatically (`LP LP 6 LK HP`). The GUI's Combos tab ships presets for every character, generated from the *Street Fighter EX 2 Plus FAQ v1.5*
by Chris MacDonald (GameFAQs) with `presets/gen_presets.py` (the FAQ itself is copyrighted and not included;
run the script on your own copy). Air-only moves and follow-ups such as "during X" cannot be expressed as a
plain sequence and are skipped. `P` / `K` in the FAQ mean any punch / kick; presets use HP / HK.
The tab's "Load character" box loads every move of the picked character into the slots right away (Meteor Combos
first, then supers, motion specials, command attacks, guard break and throws; triggers, player and facing stay as
they are). The "Type" column shows the move type. Default triggers are the number-row keys 1 2 3 5 6 8 9 0 on the
first eight slots (Test and Service are F4 and F7); the default set is Ryu's.

## Hotkeys
Only while the game window has the focus:
- **Pause** pauses the emulation (**Esc** during a pause ends the pause and ZiNc quits as usual) (the window title says so) and resumes it. The sound of the game is muted while paused (through its Windows audio session, so it works with any sound plugin; needs Windows Vista or newer and a sound device).
- **Alt+Enter** switches between the window and fullscreen. For the OpenGL / Direct3D renderers the plugin switches the screen to the resolution of the game window when the display supports it (4:3 resolutions such as 640x480, 800x600 or 1024x768 fill the screen exactly) and puts the window back and restores the old mode when you press it again; otherwise a borderless window covers the monitor. The Direct3D 6 (DirectDraw) renderer cannot take either: its surfaces are lost when the screen mode changes and it always draws at the window size, so there the window keeps its size, goes borderless and sits centered on a black screen. The Direct3D 11 renderer handles this key
  itself (it marks the window with the property `ZincFullscreenHandled`, then the plugin leaves it alone) and scales the picture;
  a 4:3 resolution stays 4:3 (black bars) on a wide screen. Alt+Enter never counts as Start.

## Autofire
`p1_b1_auto=1` (any of `p1_b1`..`p1_b6`, `p2_b1`..`p2_b6`) makes that button pulse while it is held; `autofire_ms=33`
is the length of each on / off half period (16-500). The Controls tab has an Autofire column for it.
