#!/bin/sh
# Builds ZiNc-EX.exe (64-bit) and ZiNc-EX-32bit.exe:   sh build.sh
#
# Uses Zig's bundled C compiler / resource compiler (python3 -m pip install ziglang), no Windows needed.
# With a mingw-w64 toolchain instead:
#   windres app.rc -O coff -o app.o
#   x86_64-w64-mingw32-gcc -Os -s -municode -mwindows -o ZiNc-EX.exe $SRC app.o $LIBS
#   i686-w64-mingw32-gcc   -Os -s -municode -mwindows -o ZiNc-EX-32bit.exe $SRC app.o $LIBS
set -e
cd "$(dirname "$0")"
SRC="main.c ui.c util.c settings.c games.c input.c controls.c combos.c combo_data.c tips.c embed.c profiles.c gamecfg.c zipio.c romcheck.c classify.c backup.c theme.c icon_data.c toolbar.c gamedb.c"
LIBS="-luser32 -lgdi32 -lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid -luxtheme -lwinmm -ladvapi32"
ZIG="${ZIG:-python3 -m ziglang}"
# the stamp changes the compiler command every time: Zig would otherwise reuse an old embed.c object and miss new plugin files (.incbin)
FLAGS="-Os -s -municode -Wl,--subsystem,windows -Wall -Wno-unused -Wno-parentheses -DBUILD_STAMP=$(date +%s)"
$ZIG rc /fo app.res app.rc
$ZIG cc -target x86_64-windows-gnu $FLAGS -o ZiNc-EX.exe $SRC app.res $LIBS
$ZIG cc -target x86-windows-gnu $FLAGS -o ZiNc-EX-32bit.exe $SRC app.res $LIBS
rm -f app.res
