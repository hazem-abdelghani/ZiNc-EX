@echo off
rem Builds ZiNc-EX.exe (64-bit) and ZiNc-EX-32bit.exe on Windows with Zig:   build.bat
rem Needs Python and "python -m pip install ziglang" (or set ZIG to another zig command, e.g. set ZIG=zig).
setlocal
cd /d "%~dp0"
if "%ZIG%"=="" set ZIG=python -m ziglang
set SRC=main.c ui.c util.c settings.c games.c input.c controls.c combos.c combo_data.c tips.c embed.c profiles.c gamecfg.c zipio.c romcheck.c classify.c backup.c theme.c icon_data.c toolbar.c gamedb.c
set LIBS=-luser32 -lgdi32 -lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid -luxtheme -lwinmm -ladvapi32
rem the stamp changes the compiler command every time, so a new plugin file (.incbin in embed.c) is never missed
set FLAGS=-Os -s -municode -Wl,--subsystem,windows -Wall -Wno-unused -Wno-parentheses -DBUILD_STAMP=%RANDOM%%RANDOM%
%ZIG% rc /fo app.res app.rc || goto :fail
%ZIG% cc -target x86_64-windows-gnu %FLAGS% -o ZiNc-EX.exe %SRC% app.res %LIBS% || goto :fail
%ZIG% cc -target x86-windows-gnu %FLAGS% -o ZiNc-EX-32bit.exe %SRC% app.res %LIBS% || goto :fail
del app.res
echo.
echo Done: ZiNc-EX.exe and ZiNc-EX-32bit.exe
exit /b 0
:fail
echo.
echo Build failed.
exit /b 1
